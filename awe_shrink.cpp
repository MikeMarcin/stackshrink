#include <boost/coroutine2/all.hpp>
#include <intrin.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <Psapi.h>
#include <chrono>
#include <tchar.h>

template< typename T >
struct hex_fmt_t {
    hex_fmt_t( T x ) : val( x ) {}
    friend std::ostream& operator<<( std::ostream& os, const hex_fmt_t & v ) {
        return os  << std::hex
                   << std::internal
                   << std::showbase
                   << std::setw(8)
                   << std::setfill( '0' )
                   << v.val;
    }
    T val;
};

template< typename T >
hex_fmt_t<T> fmt_hex( T x ) {
    return hex_fmt_t<T>{ x };
}

const char * fmt_state( DWORD state ) {
    switch ( state ) {
        case MEM_COMMIT: return "COMMIT ";
        case MEM_FREE: return "FREE   ";
        case MEM_RESERVE: return "RESERVE";
        default: return "unknown";
    }
}

const char * fmt_type( DWORD state ) {
    switch ( state ) {
    case MEM_IMAGE: return "IMAGE ";
    case MEM_MAPPED: return "FMAPPED   ";
    case MEM_PRIVATE: return "PRIVATE";
    default: return "unknown";
    }
}

void DbgDumpStack( PBYTE pPtr ) {
    std::cout << "### Stack Dump Start\n";
    
    const auto page_size = boost::context::stack_traits::page_size();

    // Get the stack last page.
    MEMORY_BASIC_INFORMATION stMemBasicInfo;
    BOOST_VERIFY( VirtualQuery( pPtr, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
    PBYTE pPos = (PBYTE)stMemBasicInfo.AllocationBase;
    do {
        BOOST_VERIFY( VirtualQuery( pPos, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
        BOOST_VERIFY( stMemBasicInfo.RegionSize );

        std::cout << "Range: " << fmt_hex( (SIZE_T)pPos )
                  << " - " << fmt_hex( (SIZE_T)pPos + stMemBasicInfo.RegionSize )
                  << " Protect: " << fmt_hex( stMemBasicInfo.Protect )
                  << " State: " << fmt_state( stMemBasicInfo.State )
                  //<< " Type: " << fmt_type( stMemBasicInfo.Type )
                  << std::dec
                  << " Pages: " << stMemBasicInfo.RegionSize / page_size
                  << std::endl;

        pPos += stMemBasicInfo.RegionSize;
    } while ( pPos < pPtr );
    std::cout << "### Stack Dump Finish" << std::endl;
}

__declspec(noinline) PBYTE GetStackPointer() {
    return (PBYTE)_AddressOfReturnAddress() + 8;
}

void DbgDumpStack() {
    PBYTE pPtr = GetStackPointer();
    DbgDumpStack( pPtr );
}

void StackConsume( PBYTE pPtr, DWORD dwSizeExtra ) {
    const DWORD page_size = (DWORD)boost::context::stack_traits::page_size();
    for ( ; dwSizeExtra >= page_size; dwSizeExtra -= page_size ) {
        // Move our pointer to the next page on the stack.
        pPtr -= page_size;
        // read from this pointer. If the page isn't allocated yet - it will be.
        volatile BYTE nVal = *pPtr;
    }
}

void StackConsume( DWORD dwSizeExtra ) {
    PBYTE pPtr = GetStackPointer();
    StackConsume( pPtr, dwSizeExtra );
}

class timer_t {
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
public:
    timer_t() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    double stop() {
        auto stop_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>( stop_time - start_time ).count();
    }
};

/*****************************************************************
   LoggedSetLockPagesPrivilege: a function to obtain or
   release the privilege of locking physical pages.

   Inputs:

       HANDLE hProcess: Handle for the process for which the
       privilege is needed

       BOOL bEnable: Enable (TRUE) or disable?

   Return value: TRUE indicates success, FALSE failure.

*****************************************************************/
BOOL LoggedSetLockPagesPrivilege( HANDLE hProcess, BOOL bEnable ) {
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege[1];
    } Info;

    HANDLE Token;
    BOOL Result;

    // Open the token.

    Result = OpenProcessToken( hProcess, TOKEN_ADJUST_PRIVILEGES, &Token );

    if ( Result != TRUE ) {
        _tprintf( _T("Cannot open process token.\n") );
        return FALSE;
    }

    // Enable or disable?

    Info.Count = 1;
    if ( bEnable ) {
        Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
    } else {
        Info.Privilege[0].Attributes = 0;
    }

    // Get the LUID.

    Result = LookupPrivilegeValue( NULL, SE_LOCK_MEMORY_NAME, &( Info.Privilege[0].Luid ) );

    if ( Result != TRUE ) {
        _tprintf( _T("Cannot get privilege for %s.\n"), SE_LOCK_MEMORY_NAME );
        return FALSE;
    }

    // Adjust the privilege.

    Result = AdjustTokenPrivileges( Token, FALSE, (PTOKEN_PRIVILEGES)&Info, 0, NULL, NULL );

    // Check the result.

    if ( Result != TRUE ) {
        _tprintf( _T("Cannot adjust token privileges (%u)\n"), GetLastError() );
        return FALSE;
    } else {
        if ( GetLastError() != ERROR_SUCCESS ) {
            _tprintf( _T("Cannot enable the SE_LOCK_MEMORY_NAME privilege; ") );
            _tprintf( _T("please check the local policy.\n") );
            return FALSE;
        }
    }

    CloseHandle( Token );

    return TRUE;
}

class awe_stack_pool_t {
public:
    // we 2 pages per coroutine to get initialized, and we need one full stack to be reused
    static size_t get_init_commit_size() {
        const auto one_page_size = boost::context::stack_traits::page_size();
        return one_page_size * 2;
    }

    awe_stack_pool_t( size_t num_stacks, size_t stack_size_in_bytes ) : stack_size( stack_size_in_bytes ), offset(0) {
        if ( !LoggedSetLockPagesPrivilege( GetCurrentProcess(), TRUE ) ) {
            throw std::bad_alloc();
        }
        
        auto init_commit_size = get_init_commit_size();
        size_t size_in_bytes = (num_stacks * get_init_commit_size()) + (stack_size_in_bytes);

        ULONG_PTR number_of_pages = bytes_to_pages( size_in_bytes );
        page_frame_numbers.resize( number_of_pages );
        ULONG_PTR requested_pages = number_of_pages;
        auto result = AllocateUserPhysicalPages( GetCurrentProcess(), &number_of_pages, page_frame_numbers.data() );
        if ( result != TRUE ) {
            throw std::bad_alloc();
        }
        if ( number_of_pages != requested_pages ) {
            page_frame_numbers.resize( number_of_pages );
            throw std::bad_alloc();
        }
    }
    ~awe_stack_pool_t() {
        if ( !page_frame_numbers.empty() ) {
            ULONG_PTR number_of_pages = page_frame_numbers.size();
            FreeUserPhysicalPages( GetCurrentProcess(), &number_of_pages, page_frame_numbers.data() );
        }
    }

    bool map( LPVOID virtual_address, size_t size_in_bytes ) {
        ULONG_PTR number_of_pages = bytes_to_pages( size_in_bytes );
        auto result = MapUserPhysicalPages( virtual_address, number_of_pages, page_frame_numbers.data() + offset );
        if ( result == TRUE ) {
            offset += number_of_pages;
            return true;
        }
        return false;
    }

    void unmap( LPVOID virtual_address, size_t size_in_bytes ) {
        ULONG_PTR number_of_pages = bytes_to_pages( size_in_bytes );
        BOOST_VERIFY( MapUserPhysicalPages( virtual_address, number_of_pages, nullptr ) );
        // this is obviously super sketchy, it assumes a last map first unmap ordering
        // this is true in this toy example but not in general
        // proper management of this would require a lot more work
        offset -= number_of_pages;
    }

    void map_stack() {
        auto sp = GetStackPointer();
        MEMORY_BASIC_INFORMATION stMemBasicInfo;
        BOOST_VERIFY( VirtualQuery( sp, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
        PBYTE pCur = (PBYTE)stMemBasicInfo.BaseAddress;

        PBYTE pBase = (PBYTE)stMemBasicInfo.AllocationBase;
        if ( pBase < pCur ) {
            BOOST_VERIFY( map( pBase, pCur - pBase ) );
        }
    }

    void unmap_stack() {
        auto sp = GetStackPointer();
        MEMORY_BASIC_INFORMATION stMemBasicInfo;
        BOOST_VERIFY( VirtualQuery( sp, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
        PBYTE pCur = (PBYTE)stMemBasicInfo.BaseAddress;

        PBYTE pBase = (PBYTE)stMemBasicInfo.AllocationBase;
        if ( pBase < pCur ) {
            unmap( pBase, pCur - pBase );
        }
    }

    size_t get_stack_size() const { return stack_size; }
private:
    ULONG_PTR bytes_to_pages( size_t size_in_bytes ) const {
        const auto page_size = boost::context::stack_traits::page_size();
        // round up to page size bytes and divide
        ULONG_PTR number_of_pages = (size_in_bytes + (page_size - 1)) / page_size;
        return number_of_pages;
    }
    std::vector<ULONG_PTR> page_frame_numbers;
    size_t stack_size;
    size_t offset;
};

class awe_fixedsize_stack {
private:
    awe_stack_pool_t * pool_;

public:
    typedef boost::context::stack_traits traits_type;
    typedef boost::context::stack_context stack_context;

    awe_fixedsize_stack( awe_stack_pool_t & pool ) BOOST_NOEXCEPT_OR_NOTHROW :
        pool_( &pool ) {
    }

    stack_context allocate() {
        const auto one_page_size = traits_type::page_size();
        auto size_ = pool_->get_stack_size();
        // page at bottom will be used as guard-page
        const std::size_t pages( static_cast< std::size_t >(std::floor( static_cast< float >(size_) / one_page_size )) );
        BOOST_ASSERT_MSG( 1 <= pages, "at least one page must fit into stack" );
        const std::size_t size__( pages * one_page_size );
        BOOST_ASSERT( 0 != size_ && 0 != size__ );
        BOOST_ASSERT( size__ <= size_ );

        stack_context sctx;
        void * vp = ::VirtualAlloc( 0, size__, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE );
        if ( !vp ) goto error;

        // needs at least 2 pages to fully construct the coroutine and switch to it
        
        auto init_commit_size = pool_->get_init_commit_size();
        auto pPtr = static_cast<PBYTE>(vp) + size__;
        pPtr -= init_commit_size;
        if ( !pool_->map( pPtr, init_commit_size ) ) goto cleanup;
        
        sctx.size = size__;
        sctx.sp = static_cast<char *>(vp) + sctx.size;
        return sctx;
    cleanup:
        ::VirtualFree( vp, 0, MEM_RELEASE );
    error:
        throw std::bad_alloc();
    }

    void deallocate( stack_context & sctx ) BOOST_NOEXCEPT_OR_NOTHROW {
        BOOST_ASSERT( sctx.sp );

        void * vp = static_cast< char * >(sctx.sp) - sctx.size;
        pool_->unmap( vp, pool_->get_stack_size() );
        ::VirtualFree( vp, 0, MEM_RELEASE );
    }
};

template< class T, class A > 
void destroy_reverse( std::vector<T, A> & v ) {
    while ( !v.empty() ) {
        v.pop_back();
    }
}

int main() {
    using think_co = boost::coroutines2::coroutine< void >;
    using stack_t = awe_fixedsize_stack;
    int count = 1'000'000;
    size_t stack_size = 1 * 1024 * 1024;
    std::vector<think_co::push_type> thinks;

    awe_stack_pool_t stack_pool( count, stack_size );
    stack_t stack{ stack_pool };
    thinks.reserve( count );

    int num = 0;
    for ( int i = 0; i < count; ++i ) {
        thinks.emplace_back( stack,
        [&]( think_co::pull_type& c ) {
            stack_pool.map_stack();
            StackConsume( 900 * 1024 );
            stack_pool.unmap_stack();
        } );
    }

    timer_t timer;
    for ( auto & think : thinks ) {
        think();
    }
    double elapsed = timer.stop();
    std::cout << "Thought for " << elapsed << " seconds." << std::endl;

    destroy_reverse( thinks );
    
    return 0;
}
