/*
 * All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
 * its licensors.
 *
 * For complete copyright and license terms please see the LICENSE at the root of this
 * distribution (the "License"). All use of this software is governed by the License,
 * or, if provided, by the license below or the license accompanying this file. Do not
 * remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *
 */

#include <Automation/ScriptRunnerBus.h>
#include <Automation/ScriptableImGui.h>
#include <BakedShaderVariantExampleComponent.h>
#include <SampleComponentConfig.h>
#include <SampleComponentManager.h>

#include <Atom/Feature/Material/MaterialAssignment.h>
#include <Atom/RPI.Reflect/Asset/AssetUtils.h>
#include <Atom/RPI.Reflect/Material/MaterialAsset.h>
#include <Atom/RPI.Reflect/Model/ModelAsset.h>

#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/RPISystemInterface.h>
#include <AzCore/Console/Console.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Utils/Utils.h>
#include <AzFramework/Asset/AssetProcessorMessages.h>
#include <AzFramework/Asset/AssetSystemBus.h>
#include <AzFramework/IO/LocalFileIO.h>

#include <RHI/BasicRHIComponent.h>

namespace AtomSampleViewer
{
    using namespace AZ;
    using namespace RPI;

    namespace
    {
        namespace Products
        {
            static constexpr const char ModelFilePath[] = "objects/plane.azmodel";
        } // namespace Products
    } // namespace

    void BakedShaderVariantExampleComponent::Reflect(ReflectContext* context)
    {
        if (SerializeContext* serializeContext = azrtti_cast<SerializeContext*>(context))
        {
            serializeContext->Class<BakedShaderVariantExampleComponent, CommonSampleComponentBase>()->Version(0);
        }
    }

    BakedShaderVariantExampleComponent::BakedShaderVariantExampleComponent()
        : m_imguiSidebar{"@user@/BakedShaderVariantExampleComponent/sidebar.xml"}
        , m_materialBrowser{"@user@/BakedShaderVariantExampleComponent/material_browser.xml"}
        , m_imGuiFrameTimer(FrameTimeLogSize, FrameTimeLogSize)
        , m_imGuiPassTimer(PassTimeLogSize, PassTimeLogSize)
    {
    }

    void BakedShaderVariantExampleComponent::Activate()
    {
        TickBus::Handler::BusConnect();
        m_imguiSidebar.Activate();

        m_materialBrowser.SetFilter([this](const AZ::Data::AssetInfo& assetInfo) {
            if (!AzFramework::StringFunc::Path::IsExtension(assetInfo.m_relativePath.c_str(), "azmaterial"))
            {
                return false;
            }
            return assetInfo.m_assetId.m_subId == 0;
        });
        m_materialBrowser.Activate();

        Data::AssetId modelAssetId;
        Data::AssetCatalogRequestBus::BroadcastResult(
            modelAssetId, &Data::AssetCatalogRequestBus::Events::GetAssetIdByPath, Products::ModelFilePath, nullptr, false);
        AZ_Assert(modelAssetId.IsValid(), "Failed to get model asset id: %s", Products::ModelFilePath);
        m_modelAsset.Create(modelAssetId);

        const Transform cameraTransform = Transform::CreateFromQuaternionAndTranslation(
            Quaternion::CreateFromAxisAngle(Vector3::CreateAxisZ(), AZ::Constants::Pi), Vector3{0.0f, 0.4f, 0.0f});
        AZ::TransformBus::Event(GetCameraEntityId(), &AZ::TransformBus::Events::SetWorldTM, cameraTransform);

        m_meshFeatureProcessor = Scene::GetFeatureProcessorForEntityContextId<Render::MeshFeatureProcessorInterface>(GetEntityContextId());

        InitLightingPresets(true);

        Transform meshTransform =
            Transform::CreateFromQuaternion(Quaternion::CreateFromAxisAngle(Vector3::CreateAxisX(), -AZ::Constants::HalfPi));
        m_meshHandle = m_meshFeatureProcessor->AcquireMesh(m_modelAsset, m_material);
        m_meshFeatureProcessor->SetTransform(m_meshHandle, meshTransform);

        m_rootPass = AZ::RPI::PassSystemInterface::Get()->GetRootPass();
        m_rootPass->SetTimestampQueryEnabled(true);

        SetRootVariantUsage(true);
    }

    void BakedShaderVariantExampleComponent::Deactivate()
    {
        SetRootVariantUsage(false);

        m_rootPass->SetTimestampQueryEnabled(false);

        m_meshFeatureProcessor->ReleaseMesh(m_meshHandle);

        Data::AssetBus::Handler::BusDisconnect();
        TickBus::Handler::BusDisconnect();
        m_imguiSidebar.Deactivate();

        m_material = nullptr;

        ShutdownLightingPresets();
    }

    void BakedShaderVariantExampleComponent::OnTick(float deltaTime, ScriptTimePoint /*scriptTime*/)
    {
        m_imGuiFrameTimer.PushValue(deltaTime);

        AZ::RPI::TimestampResult timestampResult = m_rootPass->GetTimestampResult();
        double gpuFrameTimeMs = aznumeric_cast<double>(timestampResult.GetTimestampInNanoseconds()) / 1000000;
        m_imGuiPassTimer.PushValue(gpuFrameTimeMs);

        bool materialNeedsUpdate = false;
        if (m_imguiSidebar.Begin())
        {
            ImGuiLightingPreset();

            ImGuiAssetBrowser::WidgetSettings assetBrowserSettings;

            assetBrowserSettings.m_labels.m_root = "Materials";
            materialNeedsUpdate |= m_materialBrowser.Tick(assetBrowserSettings);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();


            ImGui::Text("Shader Variant Usage:");
            if (ScriptableImGui::Button("Force Root Variant"))
            {
                SetRootVariantUsage(true);
            }
            ImGui::SameLine();
            if (ScriptableImGui::Button("Optimize Variant"))
            {
                SetRootVariantUsage(false);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("FPS");
            ImGuiHistogramQueue::WidgetSettings settings;
            settings.m_reportInverse = true;
            settings.m_units = "fps";
            m_imGuiFrameTimer.Tick(deltaTime, settings);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("GPU Root Pass");
            ImGuiHistogramQueue::WidgetSettings gpuMetricsSettings;
            gpuMetricsSettings.m_units = "ms";
            m_imGuiPassTimer.Tick(gpuFrameTimeMs, gpuMetricsSettings);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (m_material && ImGui::Button("Material Details..."))
            {
                m_imguiMaterialDetails.SetMaterial(m_material);
                m_imguiMaterialDetails.OpenDialog();
            }

            m_imguiSidebar.End();
        }

        m_imguiMaterialDetails.Tick();

        if (materialNeedsUpdate)
        {
            MaterialChange();
        }
    }

    void BakedShaderVariantExampleComponent::SetRootVariantUsage(bool enabled)
    {
        AZ::IConsole* console = AZ::Interface<AZ::IConsole>::Get();
        console->PerformCommand(AZStd::string::format("r_forceRootShaderVariantUsage %s", enabled ? "true" : "false").c_str());
    }

    void BakedShaderVariantExampleComponent::MaterialChange()
    {
        AZ::Data::AssetId selectedMaterialAssetId = m_materialBrowser.GetSelectedAssetId();
        if (!selectedMaterialAssetId.IsValid())
        {
            selectedMaterialAssetId =
                AZ::RPI::AssetUtils::GetAssetIdForProductPath(DefaultPbrMaterialPath, AZ::RPI::AssetUtils::TraceLevel::Error);

            if (!selectedMaterialAssetId.IsValid())
            {
                AZ_Error("BakedShaderVariantExampleComponent", false, "Failed to select material to render with.");
                return;
            }
        }
        else
        {
            AZ::Data::Asset<AZ::RPI::MaterialAsset> materialAsset;
            materialAsset.Create(selectedMaterialAssetId);
            m_material = AZ::RPI::Material::FindOrCreate(materialAsset);
            m_meshFeatureProcessor->SetMaterialAssignmentMap(m_meshHandle, m_material);
        }
    }
} // namespace AtomSampleViewer