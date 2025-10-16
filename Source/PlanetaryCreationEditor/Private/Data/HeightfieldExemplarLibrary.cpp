#include "Data/HeightfieldExemplarLibrary.h"

const TArray<FHeightfieldExemplarMeta>& UHeightfieldExemplarLibrary::GetRegisteredExemplars()
{
    static const TArray<FHeightfieldExemplarMeta> EmptyExemplars;
    return EmptyExemplars;
}
