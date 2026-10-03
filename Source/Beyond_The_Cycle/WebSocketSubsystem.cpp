#include "WebSocketSubsystem.h"

#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#include "Async/Async.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"

void UWebSocketSubsystem::Initialize(
    FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Initialize audio members
    bIsReceivingAudio = false;
    bIsAudioMuted = false;
    AudioBuffer.Empty();
    AudioComponent = nullptr;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("WebSocket Subsystem Initialized")
    );
}

void UWebSocketSubsystem::Deinitialize()
{
    if (Socket.IsValid())
    {
        Socket->Close();
        Socket.Reset();
    }

    Super::Deinitialize();
}

void UWebSocketSubsystem::Connect(
    const FString& Url)
{
    if (!FModuleManager::Get().IsModuleLoaded(
        "WebSockets"))
    {
        FModuleManager::LoadModuleChecked<
            FWebSocketsModule>(
            "WebSockets"
        );
    }

    Socket =
        FWebSocketsModule::Get()
        .CreateWebSocket(
            Url
        );

    Socket->OnConnected().AddLambda(
        [this]()
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [this]()
                {
                    if (!IsValid(this))
                    {
                        return;
                    }

                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("Connected")
                    );

                    OnConnected.Broadcast();
                });
        });

    Socket->OnConnectionError().AddLambda(
        [this](const FString& Error)
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [this, Error]()
                {
                    if (!IsValid(this))
                    {
                        return;
                    }

                    UE_LOG(
                        LogTemp,
                        Error,
                        TEXT("Connection Error: %s"),
                        *Error
                    );
                });
        });

    Socket->OnClosed().AddLambda(
        [this](
            int32 StatusCode,
            const FString& Reason,
            bool bWasClean)
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [this]()
                {
                    if (!IsValid(this))
                    {
                        return;
                    }

                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("Disconnected")
                    );

                    OnDisconnected.Broadcast();
                });
        });

    Socket->OnMessage().AddLambda(
        [this](
            const FString& Message)
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [this, Message]()
                {
                    if (!IsValid(this))
                    {
                        return;
                    }

                    HandleIncomingJson(
                        Message
                    );
                });
        });

    Socket->OnBinaryMessage().AddLambda(
        [this](
            const void* Data,
            SIZE_T Size,
            bool bIsLastFragment)
        {
            AsyncTask(
                ENamedThreads::GameThread,
                [this, Data, Size]()
                {
                    if (!IsValid(this))
                    {
                        return;
                    }

                    HandleIncomingBinary(
                        Data,
                        Size
                    );
                });
        });

    Socket->Connect();
}

void UWebSocketSubsystem::Disconnect()
{
    if (Socket.IsValid())
    {
        Socket->Close();
    }
}

void UWebSocketSubsystem::SendJsonMessage(
    const FString& Type,
    const FString& Message,
    bool bMuteAudio)
{
    if (!IsConnected())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Socket Not Connected")
        );

        return;
    }

    TSharedPtr<FJsonObject> Json =
        MakeShared<FJsonObject>();

    Json->SetStringField(
        TEXT("type"),
        Type
    );

    Json->SetStringField(
        TEXT("message"),
        Message
    );

    // Add mute flag if true
    if (bMuteAudio)
    {
        Json->SetBoolField(
            TEXT("muteAudio"),
            true
        );
    }

    FString Output;

    TSharedRef<TJsonWriter<>> Writer =
        TJsonWriterFactory<>::Create(
            &Output
        );

    FJsonSerializer::Serialize(
        Json.ToSharedRef(),
        Writer
    );

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Sending: %s"),
        *Output
    );

    Socket->Send(
        Output
    );
}

void UWebSocketSubsystem::HandleIncomingJson(
    const FString& JsonString)
{
    TSharedPtr<FJsonObject> Json;

    TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(
            JsonString
        );

    if (!FJsonSerializer::Deserialize(
        Reader,
        Json))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT(
                "Failed To Parse JSON: %s"
            ),
            *JsonString
        );

        return;
    }

    FString Type;
    FString Message;

    Json->TryGetStringField(
        TEXT("type"),
        Type
    );

    Json->TryGetStringField(
        TEXT("message"),
        Message
    );

    // Handle TTS control messages
    if (Type == TEXT("tts_start"))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("TTS started - clearing audio buffer")
        );
        AudioBuffer.Empty();
        bIsReceivingAudio = true;
    }
    else if (Type == TEXT("tts_end"))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("TTS ended - playing accumulated audio")
        );
        bIsReceivingAudio = false;
        PlayAccumulatedAudio();
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "Received JSON | Type=%s | Message=%s"
        ),
        *Type,
        *Message
    );

    OnMessageReceived.Broadcast(
        Type,
        Message
    );
}

bool UWebSocketSubsystem::IsConnected() const
{
    return Socket.IsValid() &&
           Socket->IsConnected();
}

void UWebSocketSubsystem::HandleIncomingBinary(
    const void* Data,
    SIZE_T Size)
{
    if (!Data || Size == 0)
    {
        return;
    }

    // If we're receiving TTS audio, accumulate it
    if (bIsReceivingAudio)
    {
        const uint8* ByteData = static_cast<const uint8*>(Data);
        AudioBuffer.Append(ByteData, Size);
        
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Accumulated audio chunk: %d bytes (total: %d bytes)"),
            Size,
            AudioBuffer.Num()
        );
    }
    else
    {
        // Convert raw binary data to TArray<uint8> for legacy delegate
        TArray<uint8> AudioData;
        AudioData.SetNum(Size);
        FMemory::Memcpy(
            AudioData.GetData(),
            Data,
            Size
        );

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Received Audio Data: %d bytes"),
            Size
        );

        // Broadcast audio data to Blueprint
        OnAudioReceived.Broadcast(AudioData);
    }
}

USoundWave* UWebSocketSubsystem::CreateSoundWaveFromPCM(
    const TArray<uint8>& PCMData,
    int32 SampleRate,
    int32 NumChannels)
{
    if (PCMData.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot create sound wave from empty PCM data"));
        return nullptr;
    }

    if (SampleRate <= 0 || NumChannels <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid audio parameters: SampleRate=%d, NumChannels=%d"), SampleRate, NumChannels);
        return nullptr;
    }

    // Create a new USoundWave object
    USoundWave* SoundWave = NewObject<USoundWave>(USoundWave::StaticClass());
    if (!SoundWave)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create USoundWave object"));
        return nullptr;
    }

    // Set audio format properties
    SoundWave->SetSampleRate(SampleRate);
    SoundWave->NumChannels = NumChannels;
    SoundWave->RawPCMDataSize = PCMData.Num();
    
    // Calculate duration (PCM data size / (sample rate * num channels * bytes per sample))
    // 16-bit audio = 2 bytes per sample
    const int32 BytesPerSample = 2;
    SoundWave->Duration = (float)PCMData.Num() / (float)(SampleRate * NumChannels * BytesPerSample);

    // Set sound wave to not loop
    SoundWave->bLooping = false;

    // Copy PCM data to RawPCMData
    SoundWave->RawPCMData = (uint8*)FMemory::Malloc(PCMData.Num());
    FMemory::Memcpy(SoundWave->RawPCMData, PCMData.GetData(), PCMData.Num());

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Created SoundWave: Duration=%.2fs, Size=%d bytes, SampleRate=%d, Channels=%d"),
        SoundWave->Duration,
        PCMData.Num(),
        SampleRate,
        NumChannels
    );

    return SoundWave;
}

void UWebSocketSubsystem::PlayAccumulatedAudio()
{
    if (AudioBuffer.Num() == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("No audio data to play")
        );
        return;
    }

    if (bIsAudioMuted)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Audio is muted - skipping playback of %d bytes"),
            AudioBuffer.Num()
        );
        AudioBuffer.Empty();
        return;
    }

    // Create sound wave from accumulated buffer
    USoundWave* SoundWave = CreateSoundWaveFromPCM(AudioBuffer, 48000, 1);
    
    if (!SoundWave)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Failed to create sound wave from accumulated audio")
        );
        AudioBuffer.Empty();
        return;
    }

    // Create audio component if it doesn't exist
    if (!AudioComponent)
    {
        AudioComponent = NewObject<UAudioComponent>(this);
        if (!AudioComponent)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("Failed to create audio component")
            );
            AudioBuffer.Empty();
            return;
        }
        AudioComponent->bAutoActivate = false;
        AudioComponent->RegisterComponent();
    }

    // Stop any currently playing audio
    if (AudioComponent->IsPlaying())
    {
        AudioComponent->Stop();
    }

    // Set the sound wave and play
    AudioComponent->SetSound(SoundWave);
    AudioComponent->Play();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Playing accumulated audio: %d bytes, Duration: %.2fs"),
        AudioBuffer.Num(),
        SoundWave->Duration
    );

    // Clear the buffer after playback starts
    AudioBuffer.Empty();
}

void UWebSocketSubsystem::SetAudioMuted(bool bMuted)
{
    bIsAudioMuted = bMuted;
    
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Audio %s"),
        bMuted ? TEXT("MUTED") : TEXT("UNMUTED")
    );
}