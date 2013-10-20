// bondump.cpp: BonDriver dumper
// WTFPL2 ( http://en.wikipedia.org/wiki/WTFPL#Version_2 )
#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include "IBonDriver2.h"

// Command result
#define RESULT_S_OK             0
#define RESULT_E_FAIL           1
#define RESULT_E_BADARG         2
#define RESULT_E_LOADLIBRARY    3
#define RESULT_E_CREATEBON      4
#define RESULT_E_OPENTUNER      5
#define RESULT_E_SETCHANNEL     6

class CBlockLock
{
public:
    CBlockLock(CRITICAL_SECTION *pSection) : m_pSection(pSection) { EnterCriticalSection(m_pSection); }
    ~CBlockLock() { LeaveCriticalSection(m_pSection); }
private:
    CRITICAL_SECTION *m_pSection;
};

#if 0
#include "TVCAS.h"

class CCasClient : public TVCAS::ICasClient, TVCAS::Helper::CBaseImplNoRef
{
public:
    CCasClient() : m_hLib(NULL), m_pManager(NULL) {}
    virtual ~CCasClient()
    {
        if (m_pManager) m_pManager->Release();
        if (m_hLib) FreeLibrary(m_hLib);
    }
    bool Reset()
    {
        return m_pManager ? m_pManager->Reset() : false;
    }
    bool ProcessStream(const BYTE *pSrc, DWORD srcSize, BYTE **ppDest, DWORD *pDestSize)
    {
        if (!m_hLib) {
            // Initialize
            TVCAS::CreateInstanceFunc pfCI;
            if ((m_hLib = LoadLibrary(TEXT("TVCAS_B25.tvcas"))) != NULL &&
                (pfCI = TVCAS::Helper::Module::CreateInstance(m_hLib)) != NULL &&
                (m_pManager = static_cast<TVCAS::ICasManager*>(pfCI(__uuidof(TVCAS::ICasManager)))) != NULL &&
                m_pManager->Initialize(this))
            {
                m_pManager->OpenCasCard(m_pManager->GetDefaultCasDevice());
            }
        }
        return m_pManager && m_pManager->IsCasCardOpen() &&
               m_pManager->ProcessStream(pSrc, srcSize, reinterpret_cast<void**>(ppDest), pDestSize);
    }
private:
    TVCAS_DECLARE_BASE;
    LPCWSTR GetName() const { return L"CasClient"; }
    LRESULT OnEvent(UINT Event, void *pParam) { return 0; }
    LRESULT OnError(const TVCAS::ErrorInfo *pInfo)
    {
        WCHAR debug[256];
        wsprintf(debug, L"CCasClient::OnError(): %d/%.63s/%.63s/%.63s\n", pInfo->Code,
                 pInfo->pszText ? pInfo->pszText : L"",
                 pInfo->pszAdvise ? pInfo->pszAdvise : L"",
                 pInfo->pszSystemMessage ? pInfo->pszSystemMessage : L"");
        OutputDebugString(debug);
        return 0;
    }
    void OutLog(TVCAS::LogType Type, LPCWSTR pszMessage)
    {
        WCHAR debug[256];
        wsprintf(debug, L"CCasClient::OutLog(): %d/%.127s\n", (int)Type, pszMessage ? pszMessage : L"");
        OutputDebugString(debug);
    }
    HMODULE m_hLib;
    TVCAS::ICasManager *m_pManager;
};

static CCasClient g_client;

#endif

class CStreamContext
{
public:
    CStreamContext()
        : driver(NULL)
        , channel(-1)
        , space(-1)
        , queueSize(4 * 1024 * 1024)
        , fProcessStream(false)
        , queue(NULL)
        , pBon2(NULL)
        , queueFront(0)
        , queueRear(0)
        , fQuit(FALSE)
        , fPurge(FALSE)
        , fSuppress(FALSE)
    {
        chmap[0][0] = -1;
        InitializeCriticalSection(&bonSection);
        InitializeCriticalSection(&queueSection);
    }
    ~CStreamContext()
    {
        delete [] queue;
        DeleteCriticalSection(&queueSection);
        DeleteCriticalSection(&bonSection);
    }
    bool ConvertToRealChannel(int *rch, int *rsp)
    {
        if (*rsp < 0) {
            for (int i = 0; chmap[i][0] >= 0; ++i) {
                if (*rch >= chmap[i][0]) {
                    *rch += chmap[i][1] - chmap[i][0];
                    *rsp = chmap[i][2];
                    break;
                }
            }
        }
        return *rch >= 0 && *rsp >= 0;
    }
public:
    LPCTSTR driver;
    int channel;
    int space;
    int chmap[16][3];
    int queueSize;
    bool fProcessStream;
    LPBYTE queue;
    IBonDriver2 *pBon2;
    CRITICAL_SECTION bonSection;
    CRITICAL_SECTION queueSection;
    int queueFront;
    int queueRear;
    INT_PTR fQuit;
    INT_PTR fPurge;
    INT_PTR fSuppress;
};

static CStreamContext *g_pCtx;
static INT_PTR g_fDoneQuit;

static unsigned int __stdcall ScanThread(void *pParam);
static unsigned int __stdcall WriteThread(void *pParam);
static unsigned int __stdcall StreamThread(void *pParam);

static int acp_wfprintf(FILE *file, LPCTSTR format, ...)
{
    // Refer to KB77255.
    TCHAR buf[1025];
    va_list ap;
    va_start(ap, format);
    int len = wvsprintf(buf, format, ap);
    va_end(ap);
#ifdef UNICODE
    char abuf[_countof(buf) * 2];
    if ((len = WideCharToMultiByte(CP_ACP, 0, buf, -1, abuf, _countof(abuf), NULL, NULL)) > 0) {
        return fputs(abuf, file) < 0 ? -1 : len - 1;
    }
    return -1;
#else
    return fputs(buf, file) < 0 ? -1 : len;
#endif
}

static BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        acp_wfprintf(stderr, TEXT("Quitting(%u)...\n"), dwCtrlType);
        g_pCtx->fQuit = TRUE;
        // Wait for quit-job to complete.
        for (int i = 0; !g_fDoneQuit && i < 200; ++i) {
            Sleep(50);
        }
    }
    return FALSE;
}

int _tmain(int argc, TCHAR *argv[])
{
    CStreamContext ctx;
    g_pCtx = &ctx;

    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == TEXT('-')) {
            switch (argv[i][1]) {
            case TEXT('p'):
                ctx.fProcessStream = true;
                continue;
            }
            if (i + 1 >= argc) {
                break;
            }
            switch (argv[i][1]) {
            case TEXT('d'):
                ctx.driver = argv[++i];
                continue;
            case TEXT('c'):
                ctx.channel = _ttoi(argv[++i]);
                continue;
            case TEXT('s'):
                ctx.space = _ttoi(argv[++i]);
                continue;
            case TEXT('m'):
                ++i;
                for (int j = 0; j < _countof(ctx.chmap) - 1; ++j) {
                    if (ctx.chmap[j][0] < 0) {
                        LPTSTR endp, endq;
                        ctx.chmap[j][0] = _tcstol(argv[i], &endp, 10);
                        if (endp != argv[i] && endp[0] == TEXT('c')) {
                            ctx.chmap[j][1] = _tcstol(&endp[1], &endq, 10);
                            if (endq != endp && endq[0] == TEXT('s')) {
                                ctx.chmap[j][2] = _tcstol(&endq[1], NULL, 10);
                                ctx.chmap[++j][0] = -1;
                                break;
                            }
                        }
                        ctx.chmap[j][0] = -1;
                        break;
                    }
                }
                continue;
            case TEXT('b'):
                ctx.queueSize = _ttoi(argv[++i]) * 1024;
                continue;
            }
        }
        break;
    }
    if (!ctx.driver || ctx.queueSize < 64 * 1024) {
        acp_wfprintf(stderr, TEXT("Usage: bondump -d driver [-c ch][-s space][-m ch_map][-b buf_size>=64][-p]\n"));
        return RESULT_E_BADARG;
    }
    ctx.queue = new BYTE[ctx.queueSize];

    // pseudo driver for debug
    if (!lstrcmp(ctx.driver, TEXT("-"))) {
        HANDLE hWriteThread = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, WriteThread, &ctx, 0, NULL));
        if (hWriteThread) {
            // StreamThread runs on main thread.
            StreamThread(&ctx);

            if (WaitForSingleObject(hWriteThread, 5000) == WAIT_TIMEOUT) {
                acp_wfprintf(stderr, TEXT("Terminating WriteThread...\n"));
                TerminateThread(hWriteThread, 1);
            }
            CloseHandle(hWriteThread);

            acp_wfprintf(stderr, TEXT("Done.\n"));
            g_fDoneQuit = TRUE;
            return RESULT_S_OK;
        }
        return RESULT_E_FAIL;
    }

    SetDllDirectory(TEXT(""));
    HMODULE hBon = LoadLibrary(ctx.driver);
    if (!hBon) {
        return RESULT_E_LOADLIBRARY;
    }
    int result = RESULT_E_CREATEBON;
    IBonDriver *(*pfnCreateBonDriver)() = reinterpret_cast<IBonDriver*(*)()>(GetProcAddress(hBon, "CreateBonDriver"));
    IBonDriver *pBon;
    if (pfnCreateBonDriver && (pBon = pfnCreateBonDriver()) != NULL) {
        result = RESULT_E_OPENTUNER;
        ctx.pBon2 = dynamic_cast<IBonDriver2*>(pBon);
        if (ctx.pBon2 && ctx.pBon2->OpenTuner()) {
            LPCTSTR pTuner = ctx.pBon2->GetTunerName();
            acp_wfprintf(stderr, TEXT("TunerName: %s\n"), pTuner ? pTuner : TEXT(""));
            LPCTSTR pSpace;
            for (int i = 0; (pSpace = ctx.pBon2->EnumTuningSpace(i)) != NULL; ++i) {
                acp_wfprintf(stderr, TEXT("TuningSpace[%d]: %s\n"), i, pSpace);
            }
            result = RESULT_E_SETCHANNEL;

            int rch = ctx.channel;
            int rsp = ctx.space;
            if (ctx.ConvertToRealChannel(&rch, &rsp) && ctx.pBon2->SetChannel(rsp, rch)) {
                acp_wfprintf(stderr, TEXT("CurChannel: %u\n"), ctx.pBon2->GetCurChannel());
                acp_wfprintf(stderr, TEXT("CurSpace: %u\n"), ctx.pBon2->GetCurSpace());

                result = RESULT_E_FAIL;
                HANDLE hScanThread = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, ScanThread, &ctx, 0, NULL));
                HANDLE hWriteThread = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, WriteThread, &ctx, 0, NULL));
                if (hScanThread && hWriteThread) {
                    // StreamThread runs on main thread.
                    StreamThread(&ctx);

                    if (WaitForSingleObject(hWriteThread, 5000) == WAIT_TIMEOUT) {
                        acp_wfprintf(stderr, TEXT("Terminating WriteThread...\n"));
                        TerminateThread(hWriteThread, 1);
                    }
                    CloseHandle(hWriteThread);

                    // terminates ReadFile, this is hacky but ok.
                    CBlockLock lock(&ctx.bonSection);
                    if (WaitForSingleObject(hScanThread, 100) == WAIT_TIMEOUT) {
                        acp_wfprintf(stderr, TEXT("Terminating ScanThread...\n"));
                        TerminateThread(hScanThread, 1);
                    }
                    CloseHandle(hScanThread);

                    result = RESULT_S_OK;
                }
            }
            ctx.pBon2->CloseTuner();
        }
        pBon->Release();
    }
    CloseHandle(hBon);

    if (result == RESULT_S_OK) {
        acp_wfprintf(stderr, TEXT("Done.\n"));
    }
    g_fDoneQuit = TRUE;

    return result;
}

static unsigned int __stdcall ScanThread(void *pParam)
{
    CStreamContext &ctx = *static_cast<CStreamContext*>(pParam);
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    char input[32] = {0};
    char c[2] = {0};
    DWORD dwRead;

    while (!ctx.fQuit && ReadFile(hStdin, c, 1, &dwRead, NULL)) {
        *c = dwRead ? *c : 0;
        lstrcatA(&input[min(lstrlenA(input), _countof(input) - 2)], c);
        if (*c != '\r' && *c != '\n') {
            continue;
        }
        if (input[0] == 'c' && input[1] == ' ') {
            char *endp;
            int channel = strtol(&input[2], &endp, 10);
            if (endp != &input[2]) {
                ctx.channel = channel;
                ctx.space = -1;
                if (endp[0] == ' ') {
                    ctx.space = strtol(&endp[1], NULL, 10);
                }
                // Set channel
                CBlockLock lock(&ctx.bonSection);
                ctx.fPurge = TRUE;
                for (int i = 0; ctx.fPurge && i < 100; ++i) {
                    Sleep(50);
                }
                int rch = ctx.channel;
                int rsp = ctx.space;
                if (ctx.ConvertToRealChannel(&rch, &rsp) && ctx.pBon2->SetChannel(rsp, rch)) {
                    ctx.fSuppress = FALSE;
                    acp_wfprintf(stderr, TEXT("CurChannel: %u\n"), ctx.pBon2->GetCurChannel());
                    acp_wfprintf(stderr, TEXT("CurSpace: %u\n"), ctx.pBon2->GetCurSpace());
                }
                else {
                    ctx.fSuppress = TRUE;
                    acp_wfprintf(stderr, TEXT("Error: SetChannel\n"));
                }
                ctx.pBon2->PurgeTsStream();
#ifdef TV_CAS_H
                g_client.Reset();
#endif
            }
        }
        input[0] = 0;
    }
    return 0;
}

static unsigned int __stdcall WriteThread(void *pParam)
{
    CStreamContext &ctx = *static_cast<CStreamContext*>(pParam);
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    for (;;) {
        BYTE buf[8192];
        DWORD dwBufCount = 0;
        {
            CBlockLock lock(&ctx.queueSection);
            if (ctx.fPurge) {
                FlushFileBuffers(hStdout);
                ctx.queueFront = ctx.queueRear;
                ctx.fPurge = FALSE;
            }
            if (ctx.fSuppress) {
                ctx.queueFront = ctx.queueRear;
            }
            // complete writing
            if (ctx.fQuit && ctx.queueFront == ctx.queueRear) {
                break;
            }
            LPBYTE queue = ctx.queue;
            int queueSize = ctx.queueSize;
            int front = ctx.queueFront;
            int rear = ctx.queueRear;
            for (; dwBufCount < _countof(buf) && front != rear; ++dwBufCount) {
                buf[dwBufCount] = queue[front++];
                front %= queueSize;
            }
            ctx.queueFront = front;
        }
        // always writes to stdout for checking broken (dwBufCount==0 means 'null write operation').
        DWORD dwWritten;
        if (!WriteFile(hStdout, buf, dwBufCount, &dwWritten, NULL)) {
            acp_wfprintf(stderr, TEXT("Quitting(StdoutClose)...\n"));
            ctx.fQuit = TRUE;
        }
        if (!dwBufCount) Sleep(50);
    }
    return 0;
}

static unsigned int __stdcall StreamThread(void *pParam)
{
    CStreamContext &ctx = *static_cast<CStreamContext*>(pParam);
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("bondump");
    HWND hwnd = NULL;
    if (RegisterClass(&wc)) {
        hwnd = CreateWindow(wc.lpszClassName, TEXT(""), 0, 0, 0, 0, 0, HWND_MESSAGE, 0, wc.hInstance, 0);
    }

    while (!ctx.fQuit) {
        BYTE buf[8192];
        BYTE *pStream = NULL;
        DWORD dwSize = 0;
        DWORD dwRemain = 0;
        int wait = 50;

        // pseudo driver for debug
        if (!lstrcmp(ctx.driver, TEXT("-"))) {
            pStream = buf;
            bool fOvereat;
            {
                CBlockLock lock(&ctx.queueSection);
                fOvereat = (ctx.queueFront - ctx.queueRear - 1 + ctx.queueSize) % ctx.queueSize < 16384;
            }
            if (!fOvereat) {
                wait = -1;
                if (!ReadFile(hStdin, buf, sizeof(buf), &dwSize, NULL) || !dwSize) {
                    acp_wfprintf(stderr, TEXT("Quitting(StdinClose)...\n"));
                    ctx.fQuit = TRUE;
                    break;
                }
            }
        }

        {
            CBlockLock lock(&ctx.bonSection);
            if ((pStream || ctx.pBon2->GetTsStream(&pStream, &dwSize, &dwRemain) && pStream) && dwSize) {
#ifdef TV_CAS_H
                BYTE *pDest;
                DWORD dwDestSize; 
                if (ctx.fProcessStream && g_client.ProcessStream(pStream, dwSize, &pDest, &dwDestSize)) {
                    pStream = pDest;
                    dwSize = dwDestSize;
                }
#endif
                CBlockLock lock2(&ctx.queueSection);
                LPBYTE queue = ctx.queue;
                int queueSize = ctx.queueSize;
                int rear = ctx.queueRear;
                for (DWORD i = 0; i < dwSize; ++i) {
                    queue[rear++] = pStream[i];
                    rear %= queueSize;
                }
                ctx.queueRear = rear;
                if (dwRemain) {
                    wait = 0;
                }
            }
        }
        if (wait >= 0) Sleep(wait);

        // receives WM_CLOSE from 'taskkill' etc...
        MSG msg;
        while (hwnd && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_CLOSE) {
                acp_wfprintf(stderr, TEXT("Quitting(WM_CLOSE)...\n"));
                ctx.fQuit = TRUE;
                break;
            }
            DispatchMessage(&msg);
        }
    }

    if (hwnd) DestroyWindow(hwnd);
    return 0;
}
