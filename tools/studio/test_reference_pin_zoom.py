"""Studio HTML5 asset chooser, pin and zoom integration contract.
These are source-level guards; native compilation and UI interaction still need QA.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "apps/studio-web/src"
NATIVE = ROOT / "apps/studio-native/StudioSession.cpp"


class StudioReferencePinZoomTests(unittest.TestCase):
    def source(self, relative):
        return (WEB / relative).read_text(encoding="utf-8")

    def test_asset_picker_reuses_native_catalog_and_existing_drag_mime(self):
        picker = self.source("components/AssetReferencePicker.tsx")
        assets = self.source("components/AssetsPanel.tsx")
        self.assertIn("application/x-aiverse-content-path", picker)
        self.assertIn("application/x-aiverse-content-path", assets)
        self.assertIn("invoke('studio.assets'", picker)
        self.assertIn("onChange(asset.guid);", picker)
        self.assertIn("Search assets", picker)
        self.assertIn("event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)", picker)
        self.assertIn("result.files.find(item => item.path === path)", picker)
        self.assertIn("if (!asset.guid)", picker)
        self.assertNotIn("else if (path &&", picker)

    def test_reference_fields_are_schema_typed(self):
        schema = self.source("propertyLogic.ts")
        field = self.source("components/PropertyField.tsx")
        self.assertIn("'asset'", schema)
        self.assertIn("assetKinds?", schema)
        self.assertIn("property.type === 'asset'", field)
        self.assertIn("<AssetReferencePicker", field)

    def test_pinned_inspector_does_not_write_to_global_selection(self):
        app = self.source("App.tsx")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("inspectorPin?.entity.id ?? snapshot?.selectedId", app)
        # The pinned Inspector re-reads its own entity's components by ID.
        self.assertIn("inspectorPin ? inspectorPin.inspector : snapshot?.inspector", app)
        self.assertIn("setInspectorPin(null);", app)
        self.assertIn('if (entityId.empty()) throw std::runtime_error("Missing document entity ID");', native)
        self.assertNotIn('selected_ != entityId) throw std::runtime_error("Selection changed while editing")', native)
        self.assertIn("const int index = indexFor(entityId);", native)
        self.assertIn('method == "studio.propertiesFor"', native)
        self.assertIn("propertiesFor(params.getStr", native)
        self.assertIn("invoke('studio.inspect', {entityId})", app)
        self.assertIn('method == "studio.inspect"', native)
        panel = self.source("panels/InspectorPanel.tsx")
        self.assertIn("toggleInspectorPin", panel)
        self.assertIn("aria-pressed={inspectorPinned}", panel)

    def test_native_material_reference_slots_validate_guid_and_importer(self):
        native = NATIVE.read_text(encoding="utf-8")
        validation = (ROOT/"apps/studio-native/StudioMaterialValidation.h").read_text(encoding="utf-8")
        schema = self.source("panels/materialSchema.ts")
        for key in ("texture", "normalMap", "emissiveMap", "teamMask",
                    "metallicRoughnessMap", "occlusionMap"):
            with self.subTest(key=key):
                self.assertIn('key=="'+key+'"', validation)
                self.assertIn("key:'"+key+"'", schema)
        self.assertIn("studioMaterialTextureKey(key)", native)
        self.assertIn("catalog.byGuid(value.str())", native)
        self.assertIn('asset->importer != "texture"', native)
        self.assertIn('str("content/" + asset->path)', native)
        self.assertIn('std::move(validatedPatch)', native)

    def test_asset_moves_repair_material_paths_using_guid(self):
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("oldPathByGuid.emplace(asset.guid,asset.path)", native)
        self.assertIn("relocated.emplace(\"content/\"+found->second,\"content/\"+asset.path)", native)
        self.assertIn("editor::saveMaterialLibrary(ws_,library", native)
        refs = (ROOT/"apps/studio-native/StudioAssetReferences.h").read_text(encoding="utf-8")
        self.assertIn("edits.emplace_back(name, std::move(candidate))", refs)
        self.assertIn("studioRemapMaterialReferences(library,relocated)", native)
        self.assertIn("studioCollectAssetReferences(", native)
        self.assertIn('method == "studio.assetReferences"', native)
        self.assertIn('"view.model"', refs)
        self.assertIn("ref.writable = writable", refs)
        self.assertIn("repairedMaterialReferences", native)
        self.assertIn("const std::function<void()> commitHook", native)
        self.assertIn("pendingAssetTransfer_->apply(token,decisions,commitHook)", native)
        assets = self.source("components/AssetsPanel.tsx")
        self.assertIn("response.repairedMaterialReferences", assets)

    def test_internal_move_rolls_back_file_and_meta_before_commit(self):
        transfer=(ROOT/"apps/studio-native/StudioAssetTransfers.cpp").read_text(encoding="utf-8")
        api=(ROOT/"apps/studio-native/StudioAssetTransfers.h").read_text(encoding="utf-8")
        native=NATIVE.read_text(encoding="utf-8")
        ui=self.source("components/AssetsPanel.tsx")
        self.assertIn("const std::function<void()>& beforeCommit", api)
        self.assertIn("if (type_==StudioTransferType::Move)", transfer)
        self.assertIn("if (beforeCommit) beforeCommit();", transfer)
        journal=(ROOT/"apps/studio-native/StudioMoveJournal.h").read_text(encoding="utf-8")
        self.assertIn("journal.begin(journalOps)", transfer)
        self.assertIn("journal.restoreOrCleanup()", transfer)
        self.assertIn("journal.commit()", transfer)
        self.assertIn("steps.rbegin()", journal)
        self.assertIn("fs::rename(it->destination,it->source)", journal)
        self.assertIn("materials.backup", journal)
        self.assertIn("committed.json", journal)
        self.assertIn("pending.json", journal)
        self.assertIn("recoveredProjectDir(ws_,project)", native)
        self.assertIn("recoveredProjectDir(ws_,path)", native)
        self.assertIn("editor::resolveProjectDir(input,&error)", native)
        self.assertIn("StudioMoveJournal::recover(directory)", native)
        self.assertIn("std::rethrow_exception(failure)", transfer)
        self.assertIn("StudioAssetCatalog catalog(project_); catalog.rebuild();", transfer)
        self.assertIn("Internal move only supports import, keep-both or skip", transfer)
        self.assertIn("source==='external' && !item.directory", ui)
        self.assertIn("editor::saveMaterialLibrary(ws_,library", native)

    def test_clearing_texture_reference_does_not_validate_empty_game_path(self):
        material_ops=(ROOT/"apps/editor/core/MaterialOps.cpp").read_text(encoding="utf-8")
        self.assertIn("!mat.getStr(k).empty()", material_ops)

    def test_shared_reference_registry_inventory_and_usage_panel(self):
        registry=(ROOT/"apps/studio-native/StudioAssetReferences.h").read_text(encoding="utf-8")
        native=NATIVE.read_text(encoding="utf-8")
        ui=self.source("components/AssetImportSettings.tsx")
        types=self.source("types.ts")
        self.assertIn("studioCollectAssetReferences", registry)
        self.assertIn("studioRemapMaterialReferences", registry)
        self.assertIn('append("rules.json", name, "view.model"', registry)
        self.assertIn("kStudioTextureFields", registry)
        self.assertIn('method == "studio.assetReferences"', native)
        self.assertIn("studioReferenceGraphJson(", native)
        self.assertIn("studio.assetReferences", ui)
        self.assertIn("interface StudioAssetReference", types)

    def test_move_preview_marks_readonly_dependencies_and_guards_commit(self):
        registry=(ROOT/"apps/studio-native/StudioAssetReferences.h").read_text(encoding="utf-8")
        native=NATIVE.read_text(encoding="utf-8")
        panel=self.source("components/AssetsPanel.tsx")
        types=self.source("types.ts")
        self.assertIn("studioMoveReferenceImpact", registry)
        self.assertIn("ref.path[source.size()]=='/'", registry)
        self.assertIn('preview.set("references",studioMoveReferenceImpact(preview,dependencies))', native)
        self.assertIn("if (reference.getBool(\"writable\")) continue;", native)
        self.assertIn("if (action!=\"skip\")", native)
        self.assertIn("read-only reference(s) block this move", panel)
        self.assertIn("disabled={busy||blocked.length>0}", panel)
        self.assertIn("references?: Array<{ id: number", types)

    def test_import_settings_lock_preserves_asset_context(self):
        assets = self.source("components/AssetsPanel.tsx")
        settings = self.source("components/AssetImportSettings.tsx")
        self.assertIn("pinnedAssetGuid", assets)
        self.assertIn("catalog.files.find(a=>a.guid===pinnedAssetGuid)", assets)
        self.assertIn("locked={pinnedAssetGuid!==null}", assets)
        self.assertIn("onToggleLock={toggleAssetPin}", assets)
        self.assertIn("selectedAsset?.guid ?? null", assets)
        self.assertIn("setPinnedAssetGuid(null)", assets)
        self.assertIn("aria-pressed={locked}", settings)
        self.assertIn("Lock import settings", settings)

    def test_shader_graph_pointer_drag_accounts_for_editor_zoom(self):
        graph = self.source("panels/ShaderGraphPanel.tsx")
        self.assertIn("document.documentElement.style.zoom", graph)
        self.assertIn("(event.clientX-current.x)/zoom", graph)
        self.assertIn("(event.clientY-current.y)/zoom", graph)

    def test_native_move_recovery_integration_targets_exist(self):
        cmake=(ROOT/"apps/studio-native/CMakeLists.txt").read_text(encoding="utf-8")
        cases=(ROOT/"apps/studio-native/tests/StudioMoveJournalTests.cpp").read_text(encoding="utf-8")
        transfer=(ROOT/"apps/studio-native/tests/StudioAssetTransferTests.cpp").read_text(encoding="utf-8")
        self.assertIn("add_test(NAME studio_move_journal",cmake)
        self.assertIn("StudioMoveJournal::recover(project)",cases)
        self.assertIn("journal.commit()",cases)
        self.assertIn("Studio recovery collision", (ROOT/"apps/studio-native/StudioMoveJournal.h").read_text(encoding="utf-8"))
        self.assertIn("injected material write failure",transfer)
        self.assertIn("AIVERSE_TEST_CHECK(replaceRejected)",transfer)

    def test_model_release_invalidates_renderer_caches_first(self):
        scene=(ROOT/"apps/studio-native/mac/StudioMetalScene.cpp").read_text(encoding="utf-8")
        self.assertEqual(scene.count("render::releaseModel("),1)
        release=scene[scene.index("void StudioMetalScene::releasePreviewModel"):scene.index("void StudioMetalScene::clearDocumentModels")]
        self.assertLess(release.index("invalidateModelCachesFor(&model)"),release.index("render::releaseModel"))

    def test_real_scene_model_render_and_pick_share_instances(self):
        scene=(ROOT/"apps/studio-native/mac/StudioMetalScene.cpp").read_text(encoding="utf-8")
        api=(ROOT/"apps/studio-native/mac/StudioMetalScene.h").read_text(encoding="utf-8")
        host=(ROOT/"apps/studio-native/mac/Main.mm").read_text(encoding="utf-8")
        self.assertIn('editor::authoredModelAsset(entity)',scene)
        self.assertIn('const auto& view=rules["types"][entity.type]["view"]',scene)
        self.assertIn('path.rfind("content/",0)!=0',scene)
        self.assertIn('fs::is_symlink(status)',scene)
        self.assertIn('render::loadModel(path,read)',scene)
        self.assertIn('render::uploadModel(*device_,*model,true)',scene)
        self.assertIn('syncDocumentModels(doc,projectPath,rules,proxies)',scene)
        self.assertIn('studioPickSceneSurfaces(modelInstances_,proxies,origin,direction)',scene)
        self.assertIn('renderer_.drawModel(draw)',scene)
        self.assertIn('draw.world=instance.world',scene)
        self.assertIn('modelInstances_.push_back({proxy.id',scene)
        self.assertIn('if (hasModel) continue;',scene)
        self.assertIn('clearDocumentModels();',scene)
        self.assertIn('model->skinned()',scene)
        self.assertIn('material.alpha==render::AlphaMode::Blend',scene)  # masked rigid models are allowed
        self.assertIn('model->batching.autoLod=false',scene)
        self.assertIn('rejectedDocumentModels_',api)
        self.assertIn('documentModelTimes_',api)
        self.assertIn('fs::last_write_time(current,modifiedError)',scene)
        self.assertIn('previousTime->second!=modified',scene)
        self.assertIn('session_->workspace().project.rulesJson',host)

    def test_rigid_cpu_model_triangle_picking_adapter(self):
        adapter=(ROOT/"apps/studio-native/StudioModelPicking.h").read_text(encoding="utf-8")
        cases=(ROOT/"apps/studio-native/tests/StudioModelPickingTests.cpp").read_text(encoding="utf-8")
        cmake=(ROOT/"apps/studio-native/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("studioRayTriangle",adapter)
        self.assertIn("studioPickModelSurface",adapter)
        self.assertIn("studioPickModelInstances",adapter)
        self.assertIn("studioPickSceneSurfaces",adapter)
        common=(ROOT/"engine/render/ModelSurfacePicking.h").read_text(encoding="utf-8")
        self.assertIn("return render::pickModelSurface",adapter)
        self.assertIn("world*worlds[n]",common)
        self.assertIn("if (model.vertices.empty() || model.indices.empty()",common)
        self.assertIn("node.skin>=0 || model.meshes",common)
        self.assertIn("primitive.morphTargets>0",common)
        self.assertIn("modelPickAlpha",common)
        self.assertIn("triangleBudgetPerModel",adapter)
        self.assertIn("studioPickModelInstances({",cases)
        self.assertIn("studioPickSceneSurfaces(instances,",cases)
        self.assertIn("add_test(NAME studio_model_picking",cmake)

    def test_scene_selection_hits_rendered_oriented_proxy_not_aabb(self):
        renderer=(ROOT/"apps/studio-native/mac/StudioMetalScene.cpp").read_text(encoding="utf-8")
        picker=(ROOT/"apps/studio-native/StudioVisualPicking.h").read_text(encoding="utf-8")
        cases=(ROOT/"apps/studio-native/tests/StudioVisualPickingTests.cpp").read_text(encoding="utf-8")
        cmake=(ROOT/"apps/studio-native/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("studioPickSceneSurfaces(modelInstances_,proxies,origin,direction)",renderer)
        self.assertNotIn("pickSceneRenderable(candidates,origin,direction)",renderer)
        self.assertIn("studioRayProxyBox",picker)
        self.assertIn("cosine*relative.x-sine*relative.z",picker)
        self.assertIn("nearT>=0 ? nearT : farT",picker)
        self.assertIn("corner.x<rotated.boundsMax.x",cases)
        self.assertIn("collider:rotated",cases)
        self.assertIn("studio_visual_picking",cmake)

    def test_scene_camera_navigation_is_editor_only(self):
        web=self.source("components/ViewportPlaceholder.tsx")
        types=self.source("types.ts")
        renderer=(ROOT/"apps/studio-native/mac/StudioMetalScene.cpp").read_text(encoding="utf-8")
        host=(ROOT/"apps/studio-native/mac/Main.mm").read_text(encoding="utf-8")
        panel=self.source("panels/StudioPanelRenderer.tsx")
        camera=self.source("panels/sceneCamera.ts")
        gizmo=self.source("components/SceneAxisGizmo.tsx")
        for kind in ("orbit","pan","look","move","focus","reset","view","projection"):
            self.assertIn("kind==\""+kind+"\"",host)
        self.assertIn("void StudioMetalScene::pan(",renderer)
        self.assertIn("void StudioMetalScene::look(",renderer)
        self.assertIn("camera_.target=position-fromTarget*camera_.distance",renderer)
        self.assertIn("void StudioMetalScene::move(",renderer)
        self.assertIn("void StudioMetalScene::focusSelected(",renderer)
        self.assertIn("void StudioMetalScene::resetView()",renderer)
        # Unity mapping: LMB select, Alt+LMB orbit, MMB pan, RMB fly, Alt+RMB zoom.
        self.assertIn("if (button === 1) return 'pan'",camera)
        self.assertIn("return mods.alt ? 'zoom' : 'look'",camera)
        self.assertIn("if (mods.alt) return 'orbit'",camera)
        self.assertIn("navigationFor(event.button",web)
        self.assertIn("flying.current && FLY_KEYS.has(key)",web)
        self.assertIn("stepFlySpeed(flySpeedRef.current, event.deltaY)",web)
        # Orientation gizmo is driven by native camera events, not JS-side guesses.
        self.assertIn("event === 'viewport.camera' && isSceneCameraState(payload)",web)
        self.assertIn("<SceneAxisGizmo camera={camera}",web)
        self.assertIn("viewForAxis(arm.axis, arm.sign)",gizmo)
        self.assertIn("[self sendStudioEventPayload:\"viewport.camera\" payload:payload]",host)
        self.assertIn("void StudioMetalScene::setOrientation(",renderer)
        self.assertIn("render::RTSCamera StudioMetalScene::viewCamera() const",renderer)
        self.assertIn("viewCamera().screenRay(",renderer)
        self.assertIn("window.addEventListener('keyup',keyup)",web)
        self.assertIn("window.addEventListener('blur',blur)",web)
        self.assertIn("inFlight=true",web)
        self.assertIn("requestAnimationFrame(tick)",web)
        self.assertIn("new Set(['w', 'a', 's', 'd', 'q', 'e'])",web)
        self.assertIn("kind:'focus'",web)
        self.assertIn("kind:'reset'",web)
        self.assertIn("kind: 'move'",types)
        self.assertIn("Focus selected Scene object",panel)

    def test_global_zoom_persists_and_resizes_native_viewport(self):
        app = self.source("App.tsx")
        self.assertIn("aiverse.studio.uiScale", app)
        self.assertIn("document.documentElement.style.zoom", app)
        self.assertIn("window.dispatchEvent(new Event('resize'))", app)
        self.assertIn('min="75" max="200" step="5"', app)
        viewport = self.source("components/ViewportPlaceholder.tsx")
        self.assertIn("window.addEventListener('resize', update)", viewport)
        self.assertIn("getBoundingClientRect()", viewport)


if __name__ == "__main__":
    unittest.main()
