#include <intrin.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <tchar.h>
#include <windows.h>
#include <Psapi.h>


DWORD GetPageSize() {
    SYSTEM_INFO stSysInfo;
    GetSystemInfo( &stSysInfo );
    return stSysInfo.dwPageSize;
} 

DWORD g_dwProcessorPageSize = GetPageSize();


__declspec(noinline) PBYTE GetStackPointer() {
    return (PBYTE)_AddressOfReturnAddress() + 8;
}

void StackConsume( PBYTE pPtr, DWORD dwSizeExtra ) {
    const DWORD page_size = g_dwProcessorPageSize;
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

int main() {
    int count = 1'000'000;
    using think_fn = void(*)();
    std::vector< think_fn > thinks;
    thinks.reserve( count );

    for ( int i = 0; i < count; ++i ) {
        thinks.push_back( []() {
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
