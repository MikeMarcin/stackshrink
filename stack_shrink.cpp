// Original code from https://www.codeproject.com/Articles/12644/How-to-release-memory-pages-occupied-by-the-thread

#include <windows.h>
#include <cassert>
#include <cstdio>
#include <tchar.h>
#include <intrin.h>
#include <iostream>
#include <iomanip>

DWORD g_dwProcessorPageSize;

#ifdef NDEBUG
#define VERIFY( x ) (void)(x)
#else
#define VERIFY( x ) assert(x)
#endif

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

__declspec(noinline) PBYTE GetStackPointer() {
    return (PBYTE)_AddressOfReturnAddress() + 8;
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

void DbgDumpStack()
{
//#ifdef _DEBUG
    std::cout << "### Stack Dump Start\n";
    PBYTE pPtr = GetStackPointer();
    
    // Get the stack last page.
    MEMORY_BASIC_INFORMATION stMemBasicInfo;
    VERIFY( VirtualQuery( pPtr, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
    PBYTE pPos = (PBYTE)stMemBasicInfo.AllocationBase;
    do
    {
        VERIFY( VirtualQuery( pPos, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
        VERIFY( stMemBasicInfo.RegionSize );

        std::cout << "### Range: " << fmt_hex( (SIZE_T)pPos )
                  << " - " << fmt_hex( (SIZE_T)pPos + stMemBasicInfo.RegionSize )
                  << " Protect = " << fmt_hex( stMemBasicInfo.Protect )
                  << " State = " << fmt_state( stMemBasicInfo.State )
                  << " Type = " << fmt_type( stMemBasicInfo.Type )
                  << std::dec
                  << " Pages = " << stMemBasicInfo.RegionSize / g_dwProcessorPageSize
                  << std::endl;

        pPos += stMemBasicInfo.RegionSize;
    } while ( pPos < pPtr );
    std::cout << "### Stack Dump Finish" << std::endl;
//#endif // _DEBUG
}

void StackShrink()
{
    PBYTE pPtr = GetStackPointer();

                        // Round the stack pointer to the next page,
                        // add another page extra since our function itself
                        // may consume one more page, but not more than one, let's assume that.
                        // This will be the last page we want to be allocated.
                        // There will be one more page which will be the guard page.
                        // All the following pages must be freed.

    PBYTE pAllocate = pPtr - (((SIZE_T)pPtr) % g_dwProcessorPageSize) - g_dwProcessorPageSize;
    PBYTE pGuard = pAllocate - g_dwProcessorPageSize;
    PBYTE pFree = pGuard - g_dwProcessorPageSize;

    // Get the stack last page.
    MEMORY_BASIC_INFORMATION stMemBasicInfo;
    VERIFY( VirtualQuery( pPtr, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );

    // By now stMemBasicInfo.AllocationBase must
    // contain the last (in reverse order) page of the stack.
    // NOTE - this page acts as a security page,
    // and it is never allocated (committed).
    // Even if the stack consumes all
    // its thread, and guard attribute is removed
    // from the last accessible page
    // - this page still will be inaccessible.

    // Well, let's see how many pages
    // are left unallocated on the stack.
    VERIFY( VirtualQuery( stMemBasicInfo.AllocationBase, &stMemBasicInfo, sizeof( stMemBasicInfo ) ) );
    VERIFY( stMemBasicInfo.State == MEM_RESERVE );

    PBYTE pFirstAllocated = ((PBYTE)stMemBasicInfo.BaseAddress) + stMemBasicInfo.RegionSize;
    if ( pFirstAllocated <= pFree )
    {
        // Obviously the stack doesn't
        // look the way want. Let's fix it.
        // Before we make any modification to the stack
        // let's ensure that pAllocate
        // page is already allocated, so that
        // there'll be no chance there'll be
        // STATUS_GUARD_PAGE_VIOLATION
        // while we're fixing the stack and it is
        // inconsistent.
        volatile BYTE nVal = *pAllocate;
        // now it is 100% accessible.

        // Free all the pages up to pFree (including it too).
        VERIFY( VirtualFree( pFirstAllocated, pGuard - pFirstAllocated, MEM_DECOMMIT ) );

        // Make the guard page.
        VERIFY( VirtualAlloc( pGuard, g_dwProcessorPageSize, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD ) );

        // Now the stack looks like we want it to be.
        DbgDumpStack();
    }
}

void StackConsume( DWORD dwSizeExtra )
{
    PBYTE pPtr = GetStackPointer();

    for ( ; dwSizeExtra >= g_dwProcessorPageSize; dwSizeExtra -= g_dwProcessorPageSize ) {
        // Move our pointer to the next page on the stack.
        pPtr -= g_dwProcessorPageSize;
        // read from this pointer. If the page
        // isn't allocated yet - it will be.
        volatile BYTE nVal = *pPtr;
    }
    DbgDumpStack();
}


int main()
{
    SYSTEM_INFO stSysInfo;
    GetSystemInfo( &stSysInfo );
    VERIFY( stSysInfo.dwPageSize );
    g_dwProcessorPageSize = stSysInfo.dwPageSize;

    DbgDumpStack();
    std::cout << "Default stack\n" << std::endl;
    StackConsume( 3 * 4 * 1024 );


    StackConsume( 100 * 1024 );
    std::cout << "100K consumed\n" << std::endl;

    StackShrink();
    std::cout << "Stack compacted\n" << std::endl;

    StackConsume( 900 * 1024 );
    std::cout << "900K consumed\n" << std::endl;

    StackShrink();
    std::cout << "Stack compacted\n" << std::endl;

    return 0;
}
