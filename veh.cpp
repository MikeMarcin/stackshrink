
#if 0

void co_commit_page( Coroutine self, U64 addr ) {
    // Ensures that 'addr' is allocated, and that the next page in the stack is
    // a guard page on Windows. On Windows, the guard page triggers a one-time
    // STATUS_GUARD_PAGE exception, which triggers the vectored exception
    // handler.  Since the exception handler uses the same stack as the code
    // that triggered the exception, it then uses the guard page as a stack
    // area, to prevent a double stack fault upon entering the vectored
    // exception handler.

    uint64_t psize = page_size();
    uint64_t page = page_round(addr, psize);
    uint64_t len = self->stack_end-page;
    // Commit all pages between stack_min and the current page.
    uint64_t glen = psize;
    uint64_t guard = page-glen;
    assert(page < self->stack_end);
    if (!VirtualAlloc((LPVOID)page, len, MEM_COMMIT, PAGE_READWRITE)) {
        abort();     
    }
    if (!VirtualAlloc((LPVOID)guard, glen, MEM_COMMIT, PAGE_READWRITE|PAGE_GUARD)) {
        abort();
    }
}

#endif

// Using Microsoft's intrinsics instead of inline assembly
struct _TEB
{
    NT_TIB NtTib;
    // Ignore rest of struct
};

LONG WINAPI co_fault( LPEXCEPTION_POINTERS info ) {
    auto stackBase = NtCurrentTeb()->NtTib.StackBase;
    // get this_coroutine->stack range
    PBYTE ptr = GetStackPointer();
    
	DWORD code = info->ExceptionRecord->ExceptionCode;

    if ( code == EXCEPTION_ACCESS_VIOLATION ) {
      /// \see
      /// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082(v=vs.85).aspx
      // The first element of the array contains a read-write flag that
      // indicates the type of operation that caused the access violation. If
      // this value is zero, the thread attempted to read the inaccessible data.
      // If this value is 1, the thread attempted to write to an inaccessible
      // address. If this value is 8, the thread causes a user-mode data
      // execution prevention (DEP) violation. The second array element
      // specifies the virtual address of the inaccessible data.

        constexpr ULONG_PTR access_read = 0;
        constexpr ULONG_PTR access_write = 1;
        ULONG_PTR type = info->ExceptionRecord->ExceptionInformation[0];
        if ( type == access_read || type == access_write ) {
            ULONG_PTR addr = info->ExceptionRecord->ExceptionInformation[1];
            // mark page as readable
            // return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void co_set_signals() {
    AddVectoredExceptionHandler( 1, co_fault );
}