// IBonDriver2.h: BonDriver interface 2
#ifndef INCLUDE_IBONDRIVER2_H
#define INCLUDE_IBONDRIVER2_H

class IBonDriver
{
public:
    virtual BOOL OpenTuner() = 0;
    virtual void CloseTuner() = 0;
    virtual BOOL SetChannel(BYTE bCh) = 0;
    virtual float GetSignalLevel() = 0;
    virtual DWORD WaitTsStream(DWORD dwTimeOut = 0) = 0;
    virtual DWORD GetReadyCount() = 0;
    virtual BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) = 0;
    virtual BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) = 0;
    virtual void PurgeTsStream() = 0;
    virtual void Release() = 0;
};

class IBonDriver2 : public IBonDriver
{
public:
    virtual LPCWSTR GetTunerName() = 0;
    virtual BOOL IsTunerOpening() = 0;
    virtual LPCWSTR EnumTuningSpace(DWORD dwSpace) = 0;
    virtual LPCWSTR EnumChannelName(DWORD dwSpace, DWORD dwChannel) = 0;
    virtual BOOL SetChannel(DWORD dwSpace, DWORD dwChannel) = 0;
    virtual DWORD GetCurSpace() = 0;
    virtual DWORD GetCurChannel() = 0;
};

#ifdef BONSDK_IMPLEMENT
    #define BONAPI __declspec(dllexport)
#else
    #define BONAPI __declspec(dllimport)
#endif

extern "C" BONAPI IBonDriver* CreateBonDriver();

#endif
