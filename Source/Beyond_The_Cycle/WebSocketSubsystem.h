#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "WebSocketSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSocketConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSocketDisconnected);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FSocketMessageReceived,
    const FString&,
    Type,
    const FString&,
    Message
);

// Delegate for binary audio data (still available if needed)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FSocketAudioReceived,
    const TArray<uint8>&,
    AudioData
);

UCLASS()
class BEYOND_THE_CYCLE_API UWebSocketSubsystem
    : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:

    virtual void Initialize(
        FSubsystemCollectionBase& Collection
    ) override;

    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable)
    void Connect(
        const FString& Url
    );

    UFUNCTION(BlueprintCallable)
    void Disconnect();

    UFUNCTION(BlueprintCallable)
    void SendJsonMessage(
        const FString& Type,
        const FString& Message,
        bool bMuteAudio = false
    );

    UFUNCTION(BlueprintPure)
    bool IsConnected() const;

    // Audio conversion function
    UFUNCTION(BlueprintCallable, Category = "WebSocket Audio")
    static USoundWave* CreateSoundWaveFromPCM(
        const TArray<uint8>& PCMData,
        int32 SampleRate = 48000,
        int32 NumChannels = 1
    );

    // Mute/unmute audio playback
    UFUNCTION(BlueprintCallable, Category = "WebSocket Audio")
    void SetAudioMuted(bool bMuted);

    UFUNCTION(BlueprintPure, Category = "WebSocket Audio")
    bool IsAudioMuted() const { return bIsAudioMuted; }

public:

    UPROPERTY(BlueprintAssignable)
    FSocketConnected OnConnected;

    UPROPERTY(BlueprintAssignable)
    FSocketDisconnected OnDisconnected;

    UPROPERTY(BlueprintAssignable)
    FSocketMessageReceived OnMessageReceived;

    UPROPERTY(BlueprintAssignable)
    FSocketAudioReceived OnAudioReceived;

private:

    void HandleIncomingJson(
        const FString& JsonString
    );

    void HandleIncomingBinary(
        const void* Data,
        SIZE_T Size
    );

    void PlayAccumulatedAudio();

private:

    TSharedPtr<IWebSocket> Socket;
    
    // Audio accumulation buffer
    TArray<uint8> AudioBuffer;
    bool bIsReceivingAudio;
    bool bIsAudioMuted;
    
    // Audio component for playback
    UPROPERTY()
    UAudioComponent* AudioComponent;
};