#include "MCPCommandHandlers.h"

#include "ActorEditorUtils.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "MCPFileLogger.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "MCPConstants.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Materials/MaterialInstanceConstant.h"
#include "EditorAssetLibrary.h"   // 用于获取元数据标签


//
// FMCPGetSceneInfoHandler
//
TSharedPtr<FJsonObject> FMCPGetSceneInfoHandler::Execute(const TSharedPtr<FJsonObject> &Params, FSocket *ClientSocket)
{
    MCP_LOG_INFO("Handling get_scene_info command");

    UWorld *World = GEditor->GetEditorWorldContext().World();
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> ActorsArray;

    int32 ActorCount = 0;
    int32 TotalActorCount = 0;
    bool bLimitReached = false;

    // First count the total number of actors
    for (TActorIterator<AActor> CountIt(World); CountIt; ++CountIt)
    {
        TotalActorCount++;
    }

    // Then collect actor info up to the limit
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor *Actor = *It;
        TSharedPtr<FJsonObject> ActorInfo = MakeShared<FJsonObject>();
        ActorInfo->SetStringField("name", Actor->GetName());
        ActorInfo->SetStringField("type", Actor->GetClass()->GetName());

        // Add the actor label (user-facing friendly name)
        ActorInfo->SetStringField("label", Actor->GetActorLabel());

        // Add location
        FVector Location = Actor->GetActorLocation();
        TArray<TSharedPtr<FJsonValue>> LocationArray;
        LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
        LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
        LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
        ActorInfo->SetArrayField("location", LocationArray);

        ActorsArray.Add(MakeShared<FJsonValueObject>(ActorInfo));
        ActorCount++;
        if (ActorCount >= MCPConstants::MAX_ACTORS_IN_SCENE_INFO)
        {
            bLimitReached = true;
            MCP_LOG_WARNING("Actor limit reached (%d). Only returning %d of %d actors.",
                            MCPConstants::MAX_ACTORS_IN_SCENE_INFO, ActorCount, TotalActorCount);
            break; // Limit for performance
        }
    }

    Result->SetStringField("level", World->GetName());
    Result->SetNumberField("actor_count", TotalActorCount);
    Result->SetNumberField("returned_actor_count", ActorCount);
    Result->SetBoolField("limit_reached", bLimitReached);
    Result->SetArrayField("actors", ActorsArray);

    MCP_LOG_INFO("Sending get_scene_info response with %d/%d actors", ActorCount, TotalActorCount);

    return CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FMCPGetAsasetInfoHandler::Execute(const TSharedPtr<FJsonObject>& Params, FSocket* ClientSocket)
{
    const static TMap<FString, FTopLevelAssetPath> SupportedTypes = {
                         {TEXT("StaticMesh"), FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("StaticMesh"))},
                         {TEXT("Blueprint"), FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint"))},
                         {TEXT("Material"), FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Material"))},
                     };
                     
    MCP_LOG_INFO("Handling get_asset_info command");
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> ObjectsArray;
    int32 AssetCount = 0;
    int32 TotalAssetCount = 0;
    bool bLimitReached = false;

    FString Type;
    if (!Params->TryGetStringField(FStringView(TEXT("type")), Type))
    {
        MCP_LOG_WARNING("Missing 'type' field in create_object command");
        return CreateErrorResponse("Missing 'type' field");
    }
    
    // 1. 获取 AssetRegistry 模块
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // 2. 确保资产数据已加载（编辑器环境下）
#if WITH_EDITOR
    AssetRegistry.SearchAllAssets(true);
#endif
    // 创建筛选器
    FARFilter Filter;
    FString PathFilter = TEXT("/Script/Engine");
    // 路径筛选
    Filter.PackagePaths.Add(FName(*PathFilter));
    Filter.bRecursivePaths = true;

    if ( Type == "StaticMesh")
    {
        // 获取 UStaticMesh 类的路径名并添加到过滤器
        Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    }else if ( Type == "Blueprint")
    {
        // 获取 UBlueprint 类的路径名并添加到过滤器
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    }else if ( Type == "Material")
    {
        // 为了找到所有类型的材质, 我们同时筛选 UMaterial 和 UMaterialInstanceConstant
        Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
    }
    // 3. 获取所有资产数据
    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssets(Filter, AssetDataList);

    TotalAssetCount = AssetDataList.Num();
    
    for (const FAssetData& AssetData : AssetDataList)
    {
        UObject* Asset = AssetData.GetAsset();
        if (!Asset)
        {
            continue;
        }

        // 4. 为每个资产创建 JSON 对象
        TSharedPtr<FJsonObject> AssetInfo = MakeShareable(new FJsonObject);
        
        // 5. 添加基本资产信息
        AssetInfo->SetStringField("AssetName", AssetData.AssetName.ToString());
        AssetInfo->SetStringField("ObjectPath", AssetData.GetObjectPathString());
        AssetInfo->SetStringField("AssetClass", AssetData.AssetClassPath.ToString());

        // b. 资产标签 (非常重要！)
        TArray<TSharedPtr<FJsonValue>> TagsArray;
        TMap<FName, FString> Tags = UEditorAssetLibrary::GetMetadataTagValues(Asset);
        for (const auto& TagPair : Tags)
        {
            TagsArray.Add(MakeShared<FJsonValueString>(TagPair.Key.ToString()));
        }
        AssetInfo->SetArrayField("tags", TagsArray);

        // c. 根据不同资产类型，添加特定信息
        if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
        {
            // 物理尺寸 (边界框)
            FBox BoundingBox = StaticMesh->GetBoundingBox();
            TSharedPtr<FJsonObject> BoundingBoxJson = MakeShared<FJsonObject>();
            BoundingBoxJson->SetStringField("min", BoundingBox.Min.ToString());
            BoundingBoxJson->SetStringField("max", BoundingBox.Max.ToString());
            BoundingBoxJson->SetStringField("size", BoundingBox.GetSize().ToString());
            AssetInfo->SetObjectField("dimensions", BoundingBoxJson);
            
            // 材质插槽信息
            TArray<TSharedPtr<FJsonValue>> MaterialSlotsArray;
            for (const FStaticMaterial& MaterialSlot : StaticMesh->GetStaticMaterials())
            {
                TSharedPtr<FJsonObject> SlotJson = MakeShared<FJsonObject>();
                SlotJson->SetStringField("slot_name", MaterialSlot.MaterialSlotName.ToString());
                if (MaterialSlot.MaterialInterface)
                {
                    SlotJson->SetStringField("default_material", MaterialSlot.MaterialInterface->GetPathName());
                }
                MaterialSlotsArray.Add(MakeShared<FJsonValueObject>(SlotJson));
            }
            AssetInfo->SetArrayField("material_slots", MaterialSlotsArray);
        }
        
        // 6. 添加资产到数组
        ObjectsArray.Add(MakeShareable(new FJsonValueObject(AssetInfo)));
        AssetCount++;
        if (AssetCount >= MCPConstants::MAX_ACTORS_IN_ASSET_INFO)
        {
            bLimitReached = true;
            MCP_LOG_WARNING("Actor limit reached (%d). Only returning %d of %d actors.",
                            MCPConstants::MAX_ACTORS_IN_SCENE_INFO, AssetCount, TotalAssetCount);
            break; // Limit for performance
        }
    }


    Result->SetNumberField("returned_asset_count", AssetCount);
    Result->SetBoolField("limit_reached", bLimitReached);
    Result->SetArrayField("assets", ObjectsArray);

    MCP_LOG_INFO("Sending get_scene_info response with %d assets", AssetCount);
    return CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FMCPImportAssetHandler::Execute(const TSharedPtr<FJsonObject>& Params, FSocket* ClientSocket)
{
        // 1. 创建用于返回给 Python 的 JSON 对象
    TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();

    // 2. 从传入的参数中解析所需信息
    FString FilePath;
    if (!Params->TryGetStringField("file_path", FilePath))
    {
        ResponseJson->SetStringField("status", "failed");
        ResponseJson->SetStringField("message", "Missing 'file_path' parameter.");
        return ResponseJson;
    }

    // 从文件路径中获取基础名称作为资产名
    FString AssetName = FPaths::GetBaseFilename(FilePath);
    
    const TArray<TSharedPtr<FJsonValue>>* LocationJsonArray;
    FVector ActorLocation = FVector::ZeroVector;
    if (Params->TryGetArrayField("location", LocationJsonArray) && LocationJsonArray->Num() == 3)
    {
        ActorLocation.X = (*LocationJsonArray)[0]->AsNumber();
        ActorLocation.Y = (*LocationJsonArray)[1]->AsNumber();
        ActorLocation.Z = (*LocationJsonArray)[2]->AsNumber();
    }

    // 定义在内容浏览器中的目标路径
    const FString DestinationPath = FString::Printf(TEXT("/Game/MCP_Imports/%s"), *AssetName);
    
    // --- 3. 核心逻辑：使用 AsyncTask 将导入和生成操作调度到游戏主线程 ---
    
    // 我们使用 TPromise 和 TFuture 来等待主线程任务的结果
    TPromise<FString> ImportPromise;
    TFuture<FString> ImportFuture = ImportPromise.GetFuture();

    AsyncTask(ENamedThreads::GameThread, [FilePath, DestinationPath, AssetName, ActorLocation, &ImportPromise]()
    {
        // 加载 AssetTools 模块
        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        IAssetTools& AssetTools = AssetToolsModule.Get();

        // 配置自动化导入数据
        UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
        ImportData->DestinationPath = DestinationPath;
        ImportData->Filenames.Add(FilePath);
        ImportData->bReplaceExisting = true; // 如果已存在同名资产，则覆盖它

        // 执行导入
        TArray<UObject*> ImportedAssets = AssetTools.ImportAssetsAutomated(ImportData);

        if (ImportedAssets.Num() > 0 && ImportedAssets[0] != nullptr)
        {
            UObject* ImportedAsset = ImportedAssets[0];
            // 尝试将导入的资产转换为静态网格体
            UStaticMesh* ImportedMesh = Cast<UStaticMesh>(ImportedAsset);

            if (ImportedMesh && GEditor)
            {
                // 获取当前的编辑器世界
                UWorld* World = GEditor->GetEditorWorldContext().World();
                if (World)
                {
                    // 在指定位置生成一个 StaticMeshActor
                    FActorSpawnParameters SpawnParams;
                    SpawnParams.Name = FName(*AssetName);
                    AStaticMeshActor* NewActor = World->SpawnActor<AStaticMeshActor>(ActorLocation, FRotator::ZeroRotator, SpawnParams);
                    
                    if(NewActor)
                    {
                        // 将导入的模型赋给这个 Actor
                        NewActor->GetStaticMeshComponent()->SetStaticMesh(ImportedMesh);
                        NewActor->SetActorLabel(AssetName); // 设置在场景大纲视图中的显示名称
                        NewActor->PostEditChange();
                        
                        // 任务成功，通过 Promise 返回资产的路径
                        ImportPromise.SetValue(ImportedAsset->GetPathName());
                        return;
                    }
                }
            }
        }
        
        // 如果任何一步失败，通过 Promise 返回一个空字符串
        ImportPromise.SetValue(TEXT(""));
    });

    // 在当前线程（网络线程）等待主线程任务完成
    ImportFuture.Wait();
    FString ResultAssetPath = ImportFuture.Get();

    // 4. 根据主线程的执行结果，构建最终的 JSON 响应
    if (!ResultAssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
        ResultObject->SetStringField("name", ResultAssetPath);
        
        ResponseJson->SetStringField("status", "success");
        ResponseJson->SetObjectField("result", ResultObject);
    }
    else
    {
        ResponseJson->SetStringField("status", "failed");
        ResponseJson->SetStringField("message", "Failed to import asset or spawn actor in Unreal Engine. Check logs.");
    }

    return ResponseJson;
}

//
// FMCPCreateObjectHandler
//
TSharedPtr<FJsonObject> FMCPCreateObjectHandler::Execute(const TSharedPtr<FJsonObject> &Params, FSocket *ClientSocket)
{
    UWorld *World = GEditor->GetEditorWorldContext().World();

    FString Type;
    if (!Params->TryGetStringField(FStringView(TEXT("type")), Type))
    {
        MCP_LOG_WARNING("Missing 'type' field in create_object command");
        return CreateErrorResponse("Missing 'type' field");
    }

    FString Name;
    if (!Params->TryGetStringField(FStringView(TEXT("name")), Name))
    {
        MCP_LOG_WARNING("Missing 'name' field in create_object command");
        return CreateErrorResponse("Missing 'name' field");
    }

    // Get location
    const TArray<TSharedPtr<FJsonValue>> *LocationArrayPtr = nullptr;
    if (!Params->TryGetArrayField(FStringView(TEXT("location")), LocationArrayPtr) || !LocationArrayPtr || LocationArrayPtr->Num() != 3)
    {
        MCP_LOG_WARNING("Invalid 'location' field in create_object command");
        return CreateErrorResponse("Invalid 'location' field");
    }

    FVector Location(
        (*LocationArrayPtr)[0]->AsNumber(),
        (*LocationArrayPtr)[1]->AsNumber(),
        (*LocationArrayPtr)[2]->AsNumber());

    // Convert type to lowercase for case-insensitive comparison
    FString TypeLower = Type.ToLower();

    if (Type == "StaticMeshActor")
    {
        // Get mesh path if specified
        FString MeshPath;
        Params->TryGetStringField(FStringView(TEXT("mesh")), MeshPath);

        // Get label if specified
        FString Label;
        Params->TryGetStringField(FStringView(TEXT("label")), Label);

        // Get label if specified
        FString name;
        Params->TryGetStringField(FStringView(TEXT("label")), name);
        
        // Create the actor
        TPair<AStaticMeshActor *, bool> Result = CreateStaticMeshActor(World, Location, MeshPath, Label);
    
        if (Result.Value)
        {
            // AStaticMeshActor
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField("name", Result.Key->GetName());
            ResultObj->SetStringField("label", Result.Key->GetActorLabel());
            return CreateSuccessResponse(ResultObj);
        }
        else
        {
            return CreateErrorResponse("Failed to create StaticMeshActor");
        }
    }
    else if (TypeLower == "cube")
    {
        // Create a cube actor
        FString Label;
        Params->TryGetStringField(FStringView(TEXT("label")), Label);
        TPair<AStaticMeshActor *, bool> Result = CreateCubeActor(World, Location, Label);

        if (Result.Value)
        {
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField("name", Result.Key->GetName());
            ResultObj->SetStringField("label", Result.Key->GetActorLabel());
            return CreateSuccessResponse(ResultObj);
        }
        else
        {
            return CreateErrorResponse("Failed to create cube");
        }
    }
    else
    {
        MCP_LOG_WARNING("Unsupported actor type: %s", *Type);
        return CreateErrorResponse(FString::Printf(TEXT("Unsupported actor type: %s"), *Type));
    }
}

TPair<AStaticMeshActor *, bool> FMCPCreateObjectHandler::CreateStaticMeshActor(UWorld *World, const FVector &Location, const FString &MeshPath, const FString &Label, const FString& name)
{
    if (!World)
    {
        return TPair<AStaticMeshActor *, bool>(nullptr, false);
    }

    // Create the actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = FName(name); // Auto-generate a name
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AStaticMeshActor *NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, SpawnParams);
    if (NewActor)
    {
        MCP_LOG_INFO("Created StaticMeshActor at location (%f, %f, %f)", Location.X, Location.Y, Location.Z);

        // Set mesh if specified
        if (!MeshPath.IsEmpty())
        {
            UStaticMesh *Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
            if (Mesh)
            {
                NewActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                MCP_LOG_INFO("Set mesh to %s", *MeshPath);
            }
            else
            {
                MCP_LOG_WARNING("Failed to load mesh %s", *MeshPath);
            }
        }

        // Set a descriptive label
        if (!Label.IsEmpty())
        {
            NewActor->SetActorLabel(Label);
            MCP_LOG_INFO("Set custom label to %s", *Label);
        }
        else
        {
            NewActor->SetActorLabel(FString::Printf(TEXT("MCP_StaticMesh_%d"), FMath::RandRange(1000, 9999)));
        }

        return TPair<AStaticMeshActor *, bool>(NewActor, true);
    }
    else
    {
        MCP_LOG_ERROR("Failed to create StaticMeshActor");
        return TPair<AStaticMeshActor *, bool>(nullptr, false);
    }
}

TPair<AStaticMeshActor *, bool> FMCPCreateObjectHandler::CreateCubeActor(UWorld *World, const FVector &Location, const FString &Label)
{
    if (!World)
    {
        return TPair<AStaticMeshActor *, bool>(nullptr, false);
    }

    // Create a StaticMeshActor with a cube mesh
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = NAME_None;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AStaticMeshActor *NewActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, SpawnParams);
    if (NewActor)
    {
        MCP_LOG_INFO("Created Cube at location (%f, %f, %f)", Location.X, Location.Y, Location.Z);

        // Set cube mesh
        UStaticMesh *CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (CubeMesh)
        {
            NewActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
            MCP_LOG_INFO("Set cube mesh");

            // Set a descriptive label
            if (!Label.IsEmpty())
            {
                NewActor->SetActorLabel(Label);
                MCP_LOG_INFO("Set custom label to %s", *Label);
            }
            else
            {
                NewActor->SetActorLabel(FString::Printf(TEXT("MCP_Cube_%d"), FMath::RandRange(1000, 9999)));
            }

            return TPair<AStaticMeshActor *, bool>(NewActor, true);
        }
        else
        {
            MCP_LOG_WARNING("Failed to load cube mesh");
            World->DestroyActor(NewActor);
            return TPair<AStaticMeshActor *, bool>(nullptr, false);
        }
    }
    else
    {
        MCP_LOG_ERROR("Failed to create Cube");
        return TPair<AStaticMeshActor *, bool>(nullptr, false);
    }
}

//
// FMCPModifyObjectHandler
//
TSharedPtr<FJsonObject> FMCPModifyObjectHandler::Execute(const TSharedPtr<FJsonObject> &Params, FSocket *ClientSocket)
{
    UWorld *World = GEditor->GetEditorWorldContext().World();

    FString ActorName;
    if (!Params->TryGetStringField(FStringView(TEXT("name")), ActorName))
    {
        MCP_LOG_WARNING("Missing 'name' field in modify_object command");
        return CreateErrorResponse("Missing 'name' field");
    }

    AActor *Actor = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetName() == ActorName)
        {
            Actor = *It;
            break;
        }
    }

    if (!Actor)
    {
        MCP_LOG_WARNING("Actor not found: %s", *ActorName);
        return CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    bool bModified = false;

    // Check for location update
    const TArray<TSharedPtr<FJsonValue>> *LocationArrayPtr = nullptr;
    if (Params->TryGetArrayField(FStringView(TEXT("location")), LocationArrayPtr) && LocationArrayPtr && LocationArrayPtr->Num() == 3)
    {
        FVector NewLocation(
            (*LocationArrayPtr)[0]->AsNumber(),
            (*LocationArrayPtr)[1]->AsNumber(),
            (*LocationArrayPtr)[2]->AsNumber());

        Actor->SetActorLocation(NewLocation);
        MCP_LOG_INFO("Updated location of %s to (%f, %f, %f)", *ActorName, NewLocation.X, NewLocation.Y, NewLocation.Z);
        bModified = true;
    }

    // Check for rotation update
    const TArray<TSharedPtr<FJsonValue>> *RotationArrayPtr = nullptr;
    if (Params->TryGetArrayField(FStringView(TEXT("rotation")), RotationArrayPtr) && RotationArrayPtr && RotationArrayPtr->Num() == 3)
    {
        FRotator NewRotation(
            (*RotationArrayPtr)[0]->AsNumber(),
            (*RotationArrayPtr)[1]->AsNumber(),
            (*RotationArrayPtr)[2]->AsNumber());

        Actor->SetActorRotation(NewRotation);
        MCP_LOG_INFO("Updated rotation of %s to (%f, %f, %f)", *ActorName, NewRotation.Pitch, NewRotation.Yaw, NewRotation.Roll);
        bModified = true;
    }

    // Check for scale update
    const TArray<TSharedPtr<FJsonValue>> *ScaleArrayPtr = nullptr;
    if (Params->TryGetArrayField(FStringView(TEXT("scale")), ScaleArrayPtr) && ScaleArrayPtr && ScaleArrayPtr->Num() == 3)
    {
        FVector NewScale(
            (*ScaleArrayPtr)[0]->AsNumber(),
            (*ScaleArrayPtr)[1]->AsNumber(),
            (*ScaleArrayPtr)[2]->AsNumber());

        Actor->SetActorScale3D(NewScale);
        MCP_LOG_INFO("Updated scale of %s to (%f, %f, %f)", *ActorName, NewScale.X, NewScale.Y, NewScale.Z);
        bModified = true;
    }

    if (bModified)
    {
        // Create a result object with the actor name
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField("name", Actor->GetName());

        // Return success with the result object
        return CreateSuccessResponse(Result);
    }
    else
    {
        MCP_LOG_WARNING("No modifications specified for %s", *ActorName);
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetStringField("status", "warning");
        Response->SetStringField("message", "No modifications specified");
        return Response;
    }
}

//
// FMCPDeleteObjectHandler
//
TSharedPtr<FJsonObject> FMCPDeleteObjectHandler::Execute(const TSharedPtr<FJsonObject> &Params, FSocket *ClientSocket)
{
    UWorld *World = GEditor->GetEditorWorldContext().World();

    FString ActorName;
    if (!Params->TryGetStringField(FStringView(TEXT("name")), ActorName))
    {
        MCP_LOG_WARNING("Missing 'name' field in delete_object command");
        return CreateErrorResponse("Missing 'name' field");
    }

    AActor *Actor = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetName() == ActorName)
        {
            Actor = *It;
            break;
        }
    }

    if (!Actor)
    {
        MCP_LOG_WARNING("Actor not found: %s", *ActorName);
        return CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Check if the actor can be deleted
    if (!FActorEditorUtils::IsABuilderBrush(Actor))
    {
        bool bDestroyed = World->DestroyActor(Actor);
        if (bDestroyed)
        {
            MCP_LOG_INFO("Deleted actor: %s", *ActorName);
            return CreateSuccessResponse();
        }
        else
        {
            MCP_LOG_ERROR("Failed to delete actor: %s", *ActorName);
            return CreateErrorResponse(FString::Printf(TEXT("Failed to delete actor: %s"), *ActorName));
        }
    }
    else
    {
        MCP_LOG_WARNING("Cannot delete special actor: %s", *ActorName);
        return CreateErrorResponse(FString::Printf(TEXT("Cannot delete special actor: %s"), *ActorName));
    }
}

//
// FMCPExecutePythonHandler
//
TSharedPtr<FJsonObject> FMCPExecutePythonHandler::Execute(const TSharedPtr<FJsonObject> &Params, FSocket *ClientSocket)
{
    // Check if we have code or file parameter
    FString PythonCode;
    FString PythonFile;
    bool hasCode = Params->TryGetStringField(FStringView(TEXT("code")), PythonCode);
    bool hasFile = Params->TryGetStringField(FStringView(TEXT("file")), PythonFile);

    // If code/file not found directly, check if they're in a 'data' object
    if (!hasCode && !hasFile)
    {
        const TSharedPtr<FJsonObject> *DataObject;
        if (Params->TryGetObjectField(FStringView(TEXT("data")), DataObject))
        {
            hasCode = (*DataObject)->TryGetStringField(FStringView(TEXT("code")), PythonCode);
            hasFile = (*DataObject)->TryGetStringField(FStringView(TEXT("file")), PythonFile);
        }
    }

    if (!hasCode && !hasFile)
    {
        MCP_LOG_WARNING("Missing 'code' or 'file' field in execute_python command");
        return CreateErrorResponse("Missing 'code' or 'file' field. You must provide either Python code or a file path.");
    }

    FString Result;
    bool bSuccess = false;
    FString ErrorMessage;

    if (hasCode)
    {
        // For code execution, we'll create a temporary file and execute that
        MCP_LOG_INFO("Executing Python code via temporary file");

        // Create a temporary file in the project's Saved/Temp directory
        FString TempDir = FPaths::ProjectSavedDir() / MCPConstants::PYTHON_TEMP_DIR_NAME;
        IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

        // Ensure the directory exists
        if (!PlatformFile.DirectoryExists(*TempDir))
        {
            PlatformFile.CreateDirectory(*TempDir);
        }

        // Create a unique filename for the temporary Python script
        FString TempFilePath = TempDir / FString::Printf(TEXT("%s%s.py"), MCPConstants::PYTHON_TEMP_FILE_PREFIX, *FGuid::NewGuid().ToString());

        // Add error handling wrapper to the Python code
        FString WrappedPythonCode = TEXT("import sys\n")
                                        TEXT("import traceback\n")
                                            TEXT("import unreal\n\n")
                                                TEXT("# Create output capture file\n")
                                                    TEXT("output_file = open('") +
                                    TempDir + TEXT("/output.txt', 'w')\n") TEXT("error_file = open('") + TempDir + TEXT("/error.txt', 'w')\n\n") TEXT("# Store original stdout and stderr\n") TEXT("original_stdout = sys.stdout\n") TEXT("original_stderr = sys.stderr\n\n") TEXT("# Redirect stdout and stderr\n") TEXT("sys.stdout = output_file\n") TEXT("sys.stderr = error_file\n\n") TEXT("success = True\n") TEXT("try:\n")
                                    // Instead of directly embedding the code, we'll compile it first to catch syntax errors
                                    TEXT("    # Compile the code to catch syntax errors\n") TEXT("    user_code = '''") +
                                    PythonCode + TEXT("'''\n") TEXT("    try:\n") TEXT("        code_obj = compile(user_code, '<string>', 'exec')\n") TEXT("        # Execute the compiled code\n") TEXT("        exec(code_obj)\n") TEXT("    except SyntaxError as e:\n") TEXT("        traceback.print_exc()\n") TEXT("        success = False\n") TEXT("    except Exception as e:\n") TEXT("        traceback.print_exc()\n") TEXT("        success = False\n") TEXT("except Exception as e:\n") TEXT("    traceback.print_exc()\n") TEXT("    success = False\n") TEXT("finally:\n") TEXT("    # Restore original stdout and stderr\n") TEXT("    sys.stdout = original_stdout\n") TEXT("    sys.stderr = original_stderr\n") TEXT("    output_file.close()\n") TEXT("    error_file.close()\n") TEXT("    # Write success status\n") TEXT("    with open('") + TempDir + TEXT("/status.txt', 'w') as f:\n") TEXT("        f.write('1' if success else '0')\n");

        // Write the Python code to the temporary file
        if (FFileHelper::SaveStringToFile(WrappedPythonCode, *TempFilePath))
        {
            // Execute the temporary file
            FString Command = FString::Printf(TEXT("py \"%s\""), *TempFilePath);
            GEngine->Exec(nullptr, *Command);

            // Read the output, error, and status files
            FString OutputContent;
            FString ErrorContent;
            FString StatusContent;

            FFileHelper::LoadFileToString(OutputContent, *(TempDir / TEXT("output.txt")));
            FFileHelper::LoadFileToString(ErrorContent, *(TempDir / TEXT("error.txt")));
            FFileHelper::LoadFileToString(StatusContent, *(TempDir / TEXT("status.txt")));

            bSuccess = StatusContent.TrimStartAndEnd().Equals(TEXT("1"));

            // Combine output and error for the result
            Result = OutputContent;
            ErrorMessage = ErrorContent;

            // Clean up the temporary files
            PlatformFile.DeleteFile(*TempFilePath);
            PlatformFile.DeleteFile(*(TempDir / TEXT("output.txt")));
            PlatformFile.DeleteFile(*(TempDir / TEXT("error.txt")));
            PlatformFile.DeleteFile(*(TempDir / TEXT("status.txt")));
        }
        else
        {
            MCP_LOG_ERROR("Failed to create temporary Python file at %s", *TempFilePath);
            return CreateErrorResponse(FString::Printf(TEXT("Failed to create temporary Python file at %s"), *TempFilePath));
        }
    }
    else if (hasFile)
    {
        // Execute Python file
        MCP_LOG_INFO("Executing Python file: %s", *PythonFile);

        // Create a temporary directory for output capture
        FString TempDir = FPaths::ProjectSavedDir() / MCPConstants::PYTHON_TEMP_DIR_NAME;
        IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

        // Ensure the directory exists
        if (!PlatformFile.DirectoryExists(*TempDir))
        {
            PlatformFile.CreateDirectory(*TempDir);
        }

        // Create a wrapper script that executes the file and captures output
        FString WrapperFilePath = TempDir / FString::Printf(TEXT("%s_wrapper_%s.py"), MCPConstants::PYTHON_TEMP_FILE_PREFIX, *FGuid::NewGuid().ToString());

        FString WrapperCode = TEXT("import sys\n")
                                  TEXT("import traceback\n")
                                      TEXT("import unreal\n\n")
                                          TEXT("# Create output capture file\n")
                                              TEXT("output_file = open('") +
                              TempDir + TEXT("/output.txt', 'w')\n") TEXT("error_file = open('") + TempDir + TEXT("/error.txt', 'w')\n\n") TEXT("# Store original stdout and stderr\n") TEXT("original_stdout = sys.stdout\n") TEXT("original_stderr = sys.stderr\n\n") TEXT("# Redirect stdout and stderr\n") TEXT("sys.stdout = output_file\n") TEXT("sys.stderr = error_file\n\n") TEXT("success = True\n") TEXT("try:\n") TEXT("    # Read the file content\n") TEXT("    with open('") + PythonFile.Replace(TEXT("\\"), TEXT("\\\\")) + TEXT("', 'r') as f:\n") TEXT("        file_content = f.read()\n") TEXT("    # Compile the code to catch syntax errors\n") TEXT("    try:\n") TEXT("        code_obj = compile(file_content, '") + PythonFile.Replace(TEXT("\\"), TEXT("\\\\")) + TEXT("', 'exec')\n") TEXT("        # Execute the compiled code\n") TEXT("        exec(code_obj)\n") TEXT("    except SyntaxError as e:\n") TEXT("        traceback.print_exc()\n") TEXT("        success = False\n") TEXT("    except Exception as e:\n") TEXT("        traceback.print_exc()\n") TEXT("        success = False\n") TEXT("except Exception as e:\n") TEXT("    traceback.print_exc()\n") TEXT("    success = False\n") TEXT("finally:\n") TEXT("    # Restore original stdout and stderr\n") TEXT("    sys.stdout = original_stdout\n") TEXT("    sys.stderr = original_stderr\n") TEXT("    output_file.close()\n") TEXT("    error_file.close()\n") TEXT("    # Write success status\n") TEXT("    with open('") + TempDir + TEXT("/status.txt', 'w') as f:\n") TEXT("        f.write('1' if success else '0')\n");

        if (FFileHelper::SaveStringToFile(WrapperCode, *WrapperFilePath))
        {
            // Execute the wrapper script
            FString Command = FString::Printf(TEXT("py \"%s\""), *WrapperFilePath);
            GEngine->Exec(nullptr, *Command);

            // Read the output, error, and status files
            FString OutputContent;
            FString ErrorContent;
            FString StatusContent;

            FFileHelper::LoadFileToString(OutputContent, *(TempDir / TEXT("output.txt")));
            FFileHelper::LoadFileToString(ErrorContent, *(TempDir / TEXT("error.txt")));
            FFileHelper::LoadFileToString(StatusContent, *(TempDir / TEXT("status.txt")));

            bSuccess = StatusContent.TrimStartAndEnd().Equals(TEXT("1"));

            // Combine output and error for the result
            Result = OutputContent;
            ErrorMessage = ErrorContent;

            // Clean up the temporary files
            PlatformFile.DeleteFile(*WrapperFilePath);
            PlatformFile.DeleteFile(*(TempDir / TEXT("output.txt")));
            PlatformFile.DeleteFile(*(TempDir / TEXT("error.txt")));
            PlatformFile.DeleteFile(*(TempDir / TEXT("status.txt")));
        }
        else
        {
            MCP_LOG_ERROR("Failed to create wrapper Python file at %s", *WrapperFilePath);
            return CreateErrorResponse(FString::Printf(TEXT("Failed to create wrapper Python file at %s"), *WrapperFilePath));
        }
    }

    // Create the response
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField("output", Result);

    if (bSuccess)
    {
        MCP_LOG_INFO("Python execution successful");
        return CreateSuccessResponse(ResultObj);
    }
    else
    {
        MCP_LOG_ERROR("Python execution failed: %s", *ErrorMessage);
        ResultObj->SetStringField("error", ErrorMessage);

        // We're returning a success response with error details rather than an error response
        // This allows the client to still access the output and error information
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetStringField("status", "error");
        Response->SetStringField("message", "Python execution failed with errors");
        Response->SetObjectField("result", ResultObj);
        return Response;
    }
}