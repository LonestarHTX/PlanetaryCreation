#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeightfieldExemplarLibrary.generated.h"

class UTexture2D;

/** Metadata describing a heightfield exemplar available to the editor tool. */
USTRUCT(BlueprintType)
struct FHeightfieldExemplarMeta
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heightfield")
    FName Identifier = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heightfield")
    TSoftObjectPtr<UTexture2D> HeightTexture;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heightfield")
    float ElevationScaleKm = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heightfield")
    FString Notes;
};

/** Editor helper for accessing heightfield exemplar metadata. */
UCLASS()
class UHeightfieldExemplarLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Placeholder accessor – populated in later milestones once exemplars are imported. */
    UFUNCTION(BlueprintCallable, Category = "Heightfield")
    static const TArray<FHeightfieldExemplarMeta>& GetRegisteredExemplars();
};
