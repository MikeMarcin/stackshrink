#include <boost/coroutine2/all.hpp>
#include <intrin.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <Psapi.h>

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

        std::cout << "### Range: " << fmt_hex( (SIZE_T)pPos )
                  << " - " << fmt_hex( (SIZE_T)pPos + stMemBasicInfo.RegionSize )
                  << " Protect = " << fmt_hex( stMemBasicInfo.Protect )
                  << " State = " << fmt_state( stMemBasicInfo.State )
                  << " Type = " << fmt_type( stMemBasicInfo.Type )
                  << std::dec
                  << " Pages = " << stMemBasicInfo.RegionSize / page_size
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

void StackShrink() {
    PBYTE sp = GetStackPointer();

    const auto page_size = boost::context::stack_traits::page_size();

    // Round the stack pointer to the next page, add another page extra since our function itself may consume one more page, but not
    // more than one, let's assume that. This will be the last page we want to be allocated. There will be one more page which will
    // be the guard page. All the following pages must be freed.

    PBYTE pAllocate = sp - ((uintptr_t)sp & (page_size - 1)) - page_size;
    PBYTE pGuard    = pAllocate - page_size;
    PBYTE pFree     = pGuard - page_size;

    // Get the stack last page.
    MEMORY_BASIC_INFORMATION stMemBasicInfo;
    BOOST_VERIFY( VirtualQuery( sp, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );

    // By now stMemBasicInfo.AllocationBase must contain the last (in reverse order) page of the stack.
    // NOTE - this page acts as a security page, and it is never allocated (committed).
    // Even if the stack consumes all its thread, and guard attribute is removed from the last accessible page
    // this page still will be inaccessible.

    // Well, let's see how many pages are left unallocated on the stack.
    BOOST_VERIFY( VirtualQuery( stMemBasicInfo.AllocationBase, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
    BOOST_ASSERT( stMemBasicInfo.State == MEM_RESERVE );

    PBYTE pFirstAllocated = (PBYTE)stMemBasicInfo.BaseAddress + stMemBasicInfo.RegionSize;
    if ( pFirstAllocated <= pFree ) {
        // Obviously the stack doesn't look the way want. Let's fix it. Before we make any modification to the stack let's ensure
        // that pAllocate page is already allocated, so that there'll be no chance there'll be STATUS_GUARD_PAGE_VIOLATION while
        // we're fixing the stack and it is inconsistent.
        volatile BYTE nVal = *pAllocate;
        // now it is 100% accessible.

        // Free all the pages up to pFree (including it too).
        BOOST_VERIFY( VirtualFree( pFirstAllocated, pGuard - pFirstAllocated, MEM_DECOMMIT ) );

        // Make the guard page.
        BOOST_VERIFY( VirtualAlloc( pGuard, page_size, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD ) );
    }
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
        const std::size_t pages(
            static_cast< std::size_t >(
                std::floor(
                    static_cast< float >(size_) / one_page_size )) );
        BOOST_ASSERT_MSG( 1 <= pages, "at least one page must fit into stack" );
        const std::size_t size__( pages * one_page_size );
        BOOST_ASSERT( 0 != size_ && 0 != size__ );
        BOOST_ASSERT( size__ <= size_ );

        stack_context sctx;
        void * vp = ::VirtualAlloc( 0, size__, MEM_RESERVE, PAGE_READWRITE );
        if ( !vp ) goto error;

        const auto two_page_size = one_page_size + one_page_size;
        auto pPtr = static_cast<PBYTE>( vp ) + size__;
        pPtr -= two_page_size;
        if ( !VirtualAlloc( pPtr, two_page_size, MEM_COMMIT, PAGE_READWRITE ) )  goto cleanup;

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

        void * vp = static_cast< char * >(sctx.sp) - sctx.size;
        ::VirtualFree( vp, 0, MEM_RELEASE );
    }
};

void TestStack( const char * which ) {
    std::cout << "\n@@@@@ BEGIN STACK TEST - " << which << " @@@@@" << std::endl;
    DbgDumpStack();
    std::cout << "Default stack\n" << std::endl;

    StackConsume( 100 * 1024 );
    DbgDumpStack();
    std::cout << "100K consumed\n" << std::endl;

    StackShrink();
    DbgDumpStack();
    std::cout << "Stack compacted\n" << std::endl;

    StackConsume( 900 * 1024 );
    DbgDumpStack();
    std::cout << "900K consumed\n" << std::endl;

    StackShrink();
    DbgDumpStack();
    std::cout << "Stack compacted\n" << std::endl;
    std::cout << "\n@@@@@ END STACK TEST - " << which << " @@@@@" << std::endl;
}

void DbgDumpMemoryUsage() {
    PROCESS_MEMORY_COUNTERS memCounter;
    BOOL result = GetProcessMemoryInfo( GetCurrentProcess(), &memCounter, sizeof( memCounter ) );
    std::cout << "##### Process Memory - " << std::fixed << std::setprecision( 2 ) << (double)memCounter.WorkingSetSize / (1024 * 1024) << "MB #####" << std::endl;
}

int main() {
    using think_co = boost::coroutines2::coroutine< bool >;

    reserved_fixedsize_stack stack{ 1 * 1024 * 1024 };

    std::vector<think_co::push_type> entities;
    for ( int i = 0; i < 10'000; ++i ) {
        entities.emplace_back( 
            think_co::push_type( stack,
                [&]( think_co::pull_type & c ) {
                    //TestStack( "coro" );
                    if ( c.get() ) DbgDumpStack();
                    StackConsume( 900 * 1024 );
                    c();
                    if ( c.get() ) DbgDumpStack();
                    StackShrink();
                    c();
                    if ( c.get() ) DbgDumpStack();
                    StackConsume( 900 * 1024 );
                    c();
                    if ( c.get() ) DbgDumpStack();
                } )
        );
    }
    //TestStack( "main" );

    DbgDumpMemoryUsage();
    for ( int i = 0; i < 4; ++i ) {
        bool dump = true;
        for ( auto & think : entities ) {
            think( dump );
            dump = false;
        }
        DbgDumpMemoryUsage();
    }     

    entities.clear();
    DbgDumpMemoryUsage();
    return 0;
}
