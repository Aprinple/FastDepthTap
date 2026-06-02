
#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Containers/Ticker.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogFastDepthTap, Log, All);

static TAutoConsoleVariable<int32> CVarFDT_ForceFinalColor(TEXT("fdt.ForceFinalColor"), 0, TEXT("Force CaptureSource=FinalColorHDR (0=off,1=on)"));
static TAutoConsoleVariable<int32> CVarFDT_LogToFile(TEXT("fdt.LogToFile"), 1, TEXT("Write logs to a dedicated file as well as UE log"));

class FFastDepthTapModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        DefaultLogPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs/FastDepthTap.log"));
        StartUdp();
        TickerHandle = FTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FFastDepthTapModule::Tick), 0.01f);
        FileLog(TEXT("[FDT] Startup. Default log: %s"), *DefaultLogPath);
        UE_LOG(LogFastDepthTap, Log, TEXT("[FDT] UDP ready on 127.0.0.1:7779 (fmt=npy/pfm, png=1 optional, cap=/rt=/follow=)."));
    }

    virtual void ShutdownModule() override
    {
        if (TickerHandle.IsValid())
            FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        StopUdp();
        FileLog(TEXT("[FDT] Shutdown."));
    }

private:
    FSocket* UdpSocket = nullptr;
    FDelegateHandle TickerHandle;

    FCriticalSection LogMutex;
    FString DefaultLogPath;
    FString CurrentLogPath;

    void AppendLineToFile(const FString& Path, const FString& Line)
    {
        if (Path.IsEmpty()) return;
        IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
        PF.CreateDirectoryTree(*FPaths::GetPath(Path));
        FString S = Line + LINE_TERMINATOR;
        FFileHelper::SaveStringToFile(S, *Path, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
    }

    void FileLog(const TCHAR* Fmt, ...)
    {
        if (CVarFDT_LogToFile.GetValueOnAnyThread() == 0) return;
        TCHAR Buf[4096];
        va_list ArgPtr;
        va_start(ArgPtr, Fmt);
        FCString::GetVarArgs(Buf, UE_ARRAY_COUNT(Buf), Fmt, ArgPtr);
        va_end(ArgPtr);

        const FString Stamp = FDateTime::Now().ToString(TEXT("[yyyy-MM-dd HH:mm:ss.zzz] "));
        const FString Line  = Stamp + FString(Buf);

        FScopeLock Lock(&LogMutex);
        const FString& Path = CurrentLogPath.IsEmpty() ? DefaultLogPath : CurrentLogPath;
        AppendLineToFile(Path, Line);
    }

    void StartUdp()
    {
        if (UdpSocket) return;
        ISocketSubsystem* S = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!S) return;

        UdpSocket = S->CreateSocket(NAME_DGram, TEXT("FastDepthTap-UDP"), false);
        if (!UdpSocket) return;
        UdpSocket->SetReuseAddr(true);
        UdpSocket->SetNonBlocking(true);

        TSharedRef<FInternetAddr> Addr = S->CreateInternetAddr();
        bool bValid = false;
        Addr->SetIp(TEXT("127.0.0.1"), bValid);
        Addr->SetPort(7779);

        if (!UdpSocket->Bind(*Addr))
        {
            UE_LOG(LogFastDepthTap, Error, TEXT("[FDT] Bind UDP failed on 7779"));
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(UdpSocket);
            UdpSocket = nullptr;
        }
    }

    void StopUdp()
    {
        if (UdpSocket)
        {
            UdpSocket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(UdpSocket);
            UdpSocket = nullptr;
        }
    }

    bool Tick(float /*Delta*/)
    {
        if (!UdpSocket) return true;
        uint32 PendingSize = 0;
        ISocketSubsystem* S = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        while (UdpSocket->HasPendingData(PendingSize))
        {
            int32 Read = 0;
            TArray<uint8> Buf; Buf.SetNumUninitialized(FMath::Min(PendingSize, 2048u));
            TSharedRef<FInternetAddr> Sender = S->CreateInternetAddr();
            if (UdpSocket->RecvFrom(Buf.GetData(), Buf.Num(), Read, *Sender) && Read > 0)
            {
                FString Msg = FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(Buf.GetData()))).Left(Read).TrimStartAndEnd();
                HandleCommand(Msg);
            }
        }
        return true;
    }

    static UWorld* GetPlayWorld()
    {
        if (!GEngine) return nullptr;
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
            if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
                return Ctx.World();
        return nullptr;
    }

    struct FParams
    {
        FString Dir = TEXT("E:/dump");
        int32   Seq = 1;
        FString Fmt = TEXT("npy"); // npy / pfm
        FString Base = TEXT("depth_");
        FString Log;
        // selection + follow
        FString CapName;   // cap=DepthPlanarCaptureComponent (substring match)
        FString RTName;    // rt=RT_DepthMeters_1080p (exact name)
        bool    bFollow = true; // follow=1
        // visualization
        bool    bWritePNG = false; // png=1
        float   VMin = -1.f;       // vmin
        float   VMax = -1.f;       // vmax
        float   Gamma = 1.f;       // gamma
        bool    bInvert = false;   // inv=1
    };

    static void ParseKV(const FString& Msg, FParams& P)
    {
        TArray<FString> Parts; Msg.ParseIntoArrayWS(Parts);
        for (const FString& S : Parts)
        {
            if (S.Equals(TEXT("shoot"), ESearchCase::IgnoreCase)) continue;
            FString K,V; if(!S.Split(TEXT("="), &K, &V)) continue;
            if (K.Equals(TEXT("dir"), ESearchCase::IgnoreCase)) P.Dir=V;
            else if (K.Equals(TEXT("seq"), ESearchCase::IgnoreCase)) P.Seq=FCString::Atoi(*V);
            else if (K.Equals(TEXT("fmt"), ESearchCase::IgnoreCase)) P.Fmt=V.ToLower();
            else if (K.Equals(TEXT("base"), ESearchCase::IgnoreCase)) P.Base=V;
            else if (K.Equals(TEXT("log"),  ESearchCase::IgnoreCase)) P.Log=V;
            else if (K.Equals(TEXT("cap"),  ESearchCase::IgnoreCase)) P.CapName=V;
            else if (K.Equals(TEXT("rt"),   ESearchCase::IgnoreCase)) P.RTName=V;
            else if (K.Equals(TEXT("follow"), ESearchCase::IgnoreCase)) P.bFollow=(FCString::Atoi(*V)!=0);
            else if (K.Equals(TEXT("png"), ESearchCase::IgnoreCase)) P.bWritePNG=(FCString::Atoi(*V)!=0);
            else if (K.Equals(TEXT("vmin"), ESearchCase::IgnoreCase)) P.VMin=FCString::Atof(*V);
            else if (K.Equals(TEXT("vmax"), ESearchCase::IgnoreCase)) P.VMax=FCString::Atof(*V);
            else if (K.Equals(TEXT("gamma"), ESearchCase::IgnoreCase)) P.Gamma=FCString::Atof(*V);
            else if (K.Equals(TEXT("inv"), ESearchCase::IgnoreCase)) P.bInvert=(FCString::Atoi(*V)!=0);
        }
    }

    static const TCHAR* ToFormatName(ETextureRenderTargetFormat Fmt)
    {
        switch (Fmt)
        {
        case RTF_R32f: return TEXT("RTF_R32f");
        case RTF_RGBA16f: return TEXT("RTF_RGBA16f");
        case RTF_RGBA8: return TEXT("RTF_RGBA8");
        case RTF_R16f: return TEXT("RTF_R16f");
        default: return TEXT("Other");
        }
    }

    static USceneCaptureComponent2D* FindCap(UWorld* World, const FString& CapName, const FString& RTName)
    {
        if (!World) return nullptr;
        USceneCaptureComponent2D* Fallback = nullptr;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* A = *It;
            TArray<USceneCaptureComponent2D*> Caps; A->GetComponents(Caps);
            for (auto* C : Caps)
            {
                if (!C) continue;
                bool okCap = CapName.IsEmpty() || C->GetName().Contains(CapName);
                bool okRT  = true;
                if (!RTName.IsEmpty())
                {
                    UTextureRenderTarget2D* RT = C->TextureTarget;
                    okRT = (RT && GetNameSafe(RT).Equals(RTName, ESearchCase::IgnoreCase));
                }
                if (okCap && okRT) return C;
            }
            if (!Fallback && Caps.Num()) Fallback = Caps[0];
        }
        return Fallback;
    }

    static bool ReadDepthFromRT(UTextureRenderTarget2D* RT, TArray<float>& OutDepth, int32& OutW, int32& OutH)
    {
        if (!RT) return false;
        FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
        if (!Res) return false;

        OutW = RT->SizeX; OutH = RT->SizeY;
        TArray<FLinearColor> Pixels;
        bool bOK = Res->ReadLinearColorPixels(Pixels);
        if (!bOK || Pixels.Num() != OutW*OutH)
        {
            return false;
        }
        OutDepth.SetNumUninitialized(OutW*OutH);
        for (int32 i=0;i<Pixels.Num(); ++i) { OutDepth[i] = Pixels[i].R; }
        return true;
    }

    static bool WriteNPY(const FString& Path, int32 W, int32 H, const TArray<float>& Data)
    {
        TArray<uint8> Bytes;
        auto Append = [&](const void* Ptr, int64 Sz){ int64 Off=Bytes.Num(); Bytes.AddUninitialized(Sz); FMemory::Memcpy(Bytes.GetData()+Off, Ptr, Sz); };

        const char magic[] = "\x93NUMPY";
        Append(magic, 6);
        uint8 ver[2] = {1,0};
        Append(ver, 2);

        FString dict = FString::Printf(TEXT("{'descr': '<f4', 'fortran_order': False, 'shape': (%d, %d), }"), H, W);
        FTCHARToUTF8 dict8(*dict);
        const int header_base = 10;
        const int payload_len = dict8.Length();

        TArray<uint8> headerBytes;
        headerBytes.Append((const uint8*)dict8.Get(), payload_len);
        int pad = 16 - ((header_base + 2 + headerBytes.Num()) % 16);
        if (pad == 16) pad = 0;
        if (pad > 1) { int add = pad - 1; int32 oldN = headerBytes.Num(); headerBytes.AddUninitialized(add); FMemory::Memset(headerBytes.GetData()+oldN, ' ', add); }
        headerBytes.Add('\n');

        uint16 hlen = (uint16)headerBytes.Num();
        Append(&hlen, 2);
        Append(headerBytes.GetData(), headerBytes.Num());

        for (int32 y=0; y<H; ++y)
            for (int32 x=0; x<W; ++x)
                Append(&Data[y*W + x], 4);

        return FFileHelper::SaveArrayToFile(Bytes, *Path);
    }

    static bool WritePNG_Grayscale(const FString& Path, int32 W, int32 H, const TArray<uint8>& Gray)
    {
        TArray<uint8> Png;
        TArray<FColor> RGBA; RGBA.SetNumUninitialized(W*H);
        for (int32 i=0;i<W*H;++i){ uint8 g=Gray[i]; RGBA[i]=FColor(g,g,g,255); }
        FImageUtils::CompressImageArray(W, H, RGBA, Png);
        return FFileHelper::SaveArrayToFile(Png, *Path);
    }

    static void DepthToGray(const TArray<float>& Depth, int32 W, int32 H, float VMin, float VMax, float Gamma, bool bInvert, TArray<uint8>& OutGray)
    {
        float mn = FLT_MAX, mx = -FLT_MAX;
        if (VMin < 0.f || VMax < 0.f)
        {
            for (float d : Depth)
            {
                if (d > 0 && FMath::IsFinite(d))
                {
                    mn = FMath::Min(mn, d);
                    mx = FMath::Max(mx, d);
                }
            }
            if (mn == FLT_MAX || mx == -FLT_MAX) { mn = 0.f; mx = 1.f; }
            if (VMin >= 0.f) mn = VMin;
            if (VMax >= 0.f) mx = VMax;
        }
        else { mn = VMin; mx = VMax; }
        if (mx <= mn) mx = mn + 1e-3f;

        const float inv = 1.0f / (mx - mn);
        OutGray.SetNumUninitialized(W*H);
        for (int32 i=0;i<W*H;++i)
        {
            float d = Depth[i];
            float t = (d - mn) * inv;
            t = FMath::Clamp(t, 0.f, 1.f);
            if (Gamma > 0.f && Gamma != 1.f) t = FMath::Pow(t, 1.0f/Gamma);
            if (bInvert) t = 1.0f - t;
            uint8 g = (uint8)FMath::RoundToInt(t * 255.0f);
            OutGray[i] = g;
        }
    }

    static bool EnsureDir(const FString& Dir)
    {
        return FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
    }

    void HandleCommand(const FString& Msg)
    {
        if (!Msg.StartsWith(TEXT("shoot"))) return;
        FParams P; ParseKV(Msg, P);
        CurrentLogPath = P.Log;
        FileLog(TEXT("[FDT] Command: %s"), *Msg);

        UWorld* World = GetPlayWorld();
        if (!World) { FileLog(TEXT("[FDT] No World")); return; }

        USceneCaptureComponent2D* Cap = FindCap(World, P.CapName, P.RTName);
        if (!Cap) { FileLog(TEXT("[FDT] No SceneCapture2D found (cap/rt not matched).")); return; }

        // Follow player view if requested
        if (P.bFollow)
        {
            if (APlayerController* PC = World->GetFirstPlayerController())
            if (auto* PCM = PC->PlayerCameraManager)
            {
                Cap->SetWorldLocationAndRotation(PCM->GetCameraLocation(), PCM->GetCameraRotation());
                Cap->FOVAngle = PCM->GetFOVAngle();
            }
        }

        UTextureRenderTarget2D* RT = Cap->TextureTarget;
        if (!RT) { FileLog(TEXT("[FDT] No TextureTarget on selected SceneCapture.")); return; }

        // Pose log
        const FVector  Lc = Cap->GetComponentLocation();
        const FRotator Rc = Cap->GetComponentRotation();
        FileLog(TEXT("[FDT] CapPose loc=(%.2f %.2f %.2f) rot=(%.2f %.2f %.2f) FOV=%.2f"),
            Lc.X,Lc.Y,Lc.Z, Rc.Pitch,Rc.Yaw,Rc.Roll, Cap->FOVAngle);

        if (APlayerController* PC = World->GetFirstPlayerController())
        if (auto* PCM = PC->PlayerCameraManager)
        {
            const FVector  Lp = PCM->GetCameraLocation();
            const FRotator Rp = PCM->GetCameraRotation();
            FileLog(TEXT("[FDT] PlayerView loc=(%.2f %.2f %.2f) rot=(%.2f %.2f %.2f) FOV=%.2f"),
                Lp.X,Lp.Y,Lp.Z, Rp.Pitch,Rp.Yaw,Rp.Roll, PCM->GetFOVAngle());
        }

        // Capture source control
        if (CVarFDT_ForceFinalColor.GetValueOnAnyThread())
            Cap->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
        Cap->bCaptureEveryFrame = false;
        Cap->bCaptureOnMovement = false;

        // Temporary RGBA16f if bound RT is R32f (avoid D3D11 assert)
        UTextureRenderTarget2D* UseRT = RT;
        UTextureRenderTarget2D* TempRT = nullptr;
        if (RT->RenderTargetFormat == RTF_R32f)
        {
            TempRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
            TempRT->RenderTargetFormat = RTF_RGBA16f;
            TempRT->bAutoGenerateMips = false;
            TempRT->InitCustomFormat(RT->SizeX, RT->SizeY, PF_FloatRGBA, false);
            TempRT->UpdateResourceImmediate(true);
            Cap->TextureTarget = TempRT;
            UseRT = TempRT;
            FileLog(TEXT("[FDT] Using temp RGBA16f RT to avoid D3D11 assert."));
        }

        // Log config
        FileLog(TEXT("[FDT] Cap=%s RT=%s size=%dx%d fmt=%d(%s) CaptureSource=%d EveryFrame=%d OnMove=%d"),
            *Cap->GetName(), *RT->GetName(), RT->SizeX, RT->SizeY,
            (int32)RT->RenderTargetFormat, ToFormatName(RT->RenderTargetFormat),
            (int32)Cap->CaptureSource, (int32)Cap->bCaptureEveryFrame, (int32)Cap->bCaptureOnMovement);

        // Capture + flush
        double T0 = FPlatformTime::Seconds();
        Cap->CaptureScene();
        FlushRenderingCommands();
        double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
        FileLog(TEXT("[FDT] Capture+Flush = %.2f ms"), Ms);

        // Read back
        TArray<float> Depth;
        int32 W=0,H=0;
        if (!ReadDepthFromRT(UseRT, Depth, W, H))
        {
            FileLog(TEXT("[FDT] CPU readback failed."));
            if (TempRT) Cap->TextureTarget = RT;
            return;
        }
        FileLog(TEXT("[FDT] Read %dx%d floats"), W, H);

        EnsureDir(P.Dir);
        const FString Stem = FString::Printf(TEXT("%s%04d"), *P.Base, P.Seq);

        // Save depth
        const FString DepthPath = FPaths::Combine(P.Dir, Stem + TEXT(".") + P.Fmt);
        bool ok = (P.Fmt==TEXT("npy")) ? WriteNPY(DepthPath, W, H, Depth)
                                       : /*pfm*/ [&,W,H](){
                                            FString Path = FPaths::Combine(P.Dir, Stem + TEXT(".pfm"));
                                            FString header = FString::Printf(TEXT("Pf\n%d %d\n-1.0\n"), W, H);
                                            TArray<uint8> Bytes;
                                            {
                                                FTCHARToUTF8 Hdr8(*header);
                                                Bytes.Append((uint8*)Hdr8.Get(), Hdr8.Length());
                                            }
                                            Bytes.AddUninitialized(W*H*4);
                                            FMemory::Memcpy(Bytes.GetData() + Bytes.Num() - W*H*4, Depth.GetData(), W*H*4);
                                            return FFileHelper::SaveArrayToFile(Bytes, *Path);
                                         }();
        FileLog(TEXT("[FDT] Depth saved: %s (%lld bytes)"), *DepthPath, IFileManager::Get().FileSize(*DepthPath));

        // PNG visualization
        if (P.bWritePNG)
        {
            TArray<uint8> Gray;
            DepthToGray(Depth, W, H, P.VMin, P.VMax, P.Gamma, P.bInvert, Gray);
            const FString PNGPath = FPaths::Combine(P.Dir, Stem + TEXT("_vis.png"));
            if (WritePNG_Grayscale(PNGPath, W, H, Gray))
                FileLog(TEXT("[FDT] PNG saved: %s (%lld bytes)"), *PNGPath, IFileManager::Get().FileSize(*PNGPath));
            else
                FileLog(TEXT("[FDT] PNG save failed."));
        }

        if (TempRT) Cap->TextureTarget = RT;
    }
};

IMPLEMENT_MODULE(FFastDepthTapModule, FastDepthTap)
