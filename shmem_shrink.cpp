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

class reserved_fixedsize_stack {
private:
    std::size_t     size_;

public:
    typedef boost::context::stack_traits traits_type;
    typedef boost::context::stack_context stack_context;

    reserved_fixedsize_stack( std::size_t size = traits_type::default_size() ) BOOST_NOEXCEPT_OR_NOTHROW :
    size_( size ) {
    }

    stack_context allocate() {
        const auto one_page_size = traits_type::page_size();
        // page at bottom will be used as guard-page
        const std::size_t pages( static_cast<std::size_t>(std::floor( static_cast<float>(size_) / one_page_size )) );
        BOOST_ASSERT_MSG( 1 <= pages, "at least one page must fit into stack" );
        const std::size_t size__( pages * one_page_size );
        BOOST_ASSERT( 0 != size_ && 0 != size__ );
        BOOST_ASSERT( size__ <= size_ );

        stack_context sctx;
        void * vp = ::VirtualAlloc( 0, size__, MEM_RESERVE, PAGE_READWRITE );
        if ( !vp ) goto error;

        // needs at least 2 pages to fully construct the coroutine and switch to it
        const auto init_commit_size = one_page_size + one_page_size;
        auto pPtr = static_cast<PBYTE>(vp) + size__;
        pPtr -= init_commit_size;
        if ( !VirtualAlloc( pPtr, init_commit_size, MEM_COMMIT, PAGE_READWRITE ) )  goto cleanup;

        // create guard page so the OS can catch page faults and grow our stack
        pPtr -= one_page_size;
        if ( !VirtualAlloc( pPtr, one_page_size, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD ) ) goto cleanup;

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

        void * vp = static_cast<char *>(sctx.sp) - sctx.size;
        ::VirtualFree( vp, 0, MEM_RELEASE );
    }
};

int main() {
    using think_co = boost::coroutines2::coroutine< void >;
    using stack_t = reserved_fixedsize_stack;
    int count = 1'000'000;
    size_t stack_size = 1 * 1024 * 1024;
    std::vector<think_co::push_type> thinks;

    HANDLE hfm = CreateFileMapping( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof( DWORD ), NULL );
    LPDWORD pdw1 = (LPDWORD)MapViewOfFile( hfm, FILE_MAP_WRITE, 0, 0, sizeof( DWORD ) );
    LPDWORD pdw2 = (LPDWORD)MapViewOfFile( hfm, FILE_MAP_WRITE, 0, 0, sizeof( DWORD ) );

    stack_t stack{ stack_size };
    thinks.reserve( count );

    int num = 0;
    for ( int i = 0; i < count; ++i ) {
        thinks.emplace_back( stack,
        [&]( think_co::pull_type& c ) {
            StackConsume( 900 * 1024 );
        } );
    }

    timer_t timer;
    for ( auto & think : thinks ) {
        think();
    }
    double elapsed = timer.stop();
    std::cout << "Thought for " << elapsed << " seconds." << std::endl;

    return 0;
}
