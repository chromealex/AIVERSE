#include "apps/studio-native/StudioMoveJournal.h"
#include "tests/TestCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using aiverse::studio::StudioMoveJournal;
using aiverse::studio::StudioMoveJournalOperation;

static void writeFile(const fs::path& path,const std::string& value) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if (!out || !(out<<value)) throw std::runtime_error("Failed writing test fixture");
}
static std::string readFile(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("Missing fixture: "+path.string());
    return {std::istreambuf_iterator<char>(in),{}};
}
static void expect(bool value,const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
int main() {
    const fs::path project=fs::temp_directory_path()/("studio-recovery-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto root=project/"content";
        fs::create_directories(root/"A");
        fs::create_directories(root/"B");
        fs::create_directories(project/".aiverse-cache");
        const auto original=root/"A"/"Stone.png";
        const auto moved=root/"B"/"Stone.png";
        const auto originalMeta=fs::path(original.string()+".meta");
        const auto movedMeta=fs::path(moved.string()+".meta");
        const auto materials=project/"materials.json";
        const std::string materialBefore=R"({"materials":{"stone":{"texture":"content/A/Stone.png"}}})";
        const std::string materialAfter=R"({"materials":{"stone":{"texture":"content/B/Stone.png"}}})";
        writeFile(original,"pixels");
        writeFile(originalMeta,"persistent-guid-sidecar");
        writeFile(materials,materialBefore);
        const std::vector<StudioMoveJournalOperation> operations{
            {"A/Stone.png","B/Stone.png"},
            {"A/Stone.png.meta","B/Stone.png.meta"}
        };

        // Crash after content move but before metadata move.
        {
            StudioMoveJournal journal(project);
            journal.begin(operations);
            fs::rename(original,moved);
            StudioMoveJournal::recover(project);
            expect(fs::exists(original),"File not restored after partial move");
            expect(fs::exists(originalMeta),"Metadata unexpectedly changed");
            expect(!fs::exists(moved),"Destination survives rollback");
            expect(readFile(materials)==materialBefore,"Material changed by partial move recovery");
            StudioMoveJournal::recover(project); // must be idempotent
        }
        // Crash after both renames and material write, before commit.
        {
            StudioMoveJournal journal(project);
            journal.begin(operations);
            fs::rename(original,moved);
            fs::rename(originalMeta,movedMeta);
            writeFile(materials,materialAfter);
            StudioMoveJournal::recover(project);
            expect(fs::exists(original),"File not restored from completed rename");
            expect(fs::exists(originalMeta),"Sidecar not restored from completed rename");
            expect(!fs::exists(movedMeta),"Sidecar survives rollback");
            expect(readFile(original)=="pixels","File bytes changed");
            expect(readFile(originalMeta)=="persistent-guid-sidecar","GUID metadata bytes changed");
            expect(readFile(materials)==materialBefore,"Material backup not restored");
        }
        // v4: a resource document is written before the outer move commits.
        // Recovery restores both exact original resource bytes and paths.
        {
            const auto model=root/"A"/"Model.gltf";
            const std::string before="{ \"images\": [{\"uri\":\"Stone.png\"}] }\n";
            const std::string after=R"({"images":[{"uri":"../B/Stone.png"}]})";
            writeFile(model,before);
            StudioMoveJournal journal(project);
            journal.begin(operations,false,false,{{fs::path("content/A/Model.gltf"),before,after}});
            fs::rename(original,moved);fs::rename(originalMeta,movedMeta);
            writeFile(model,after);
            StudioMoveJournal::recover(project);
            expect(readFile(model)==before,"Resource repair exact bytes not restored");
            expect(fs::exists(original)&&fs::exists(originalMeta),"Resource move not restored");
            expect(!fs::exists(moved),"Resource destination survives rollback");
            StudioMoveJournal::recover(project);
            fs::remove(model);
        }
        // v5: the repaired model itself moves; restore at whichever
        // endpoint owns it before reversing the rename.
        {
            const auto model=root/"A"/"Moved.gltf",target=root/"B"/"Moved.gltf";
            const std::string before=R"({"images":[{"uri":"Stone.png"}]})",after=R"({"images":[{"uri":"../A/Stone.png"}]})";
            writeFile(model,before);
            StudioMoveJournal journal(project);
            journal.begin({{"A/Moved.gltf","B/Moved.gltf"}},false,false,{{fs::path("content/A/Moved.gltf"),before,after}});
            // Crash after the rename but before resource publication.
            fs::rename(model,target);
            StudioMoveJournal::recover(project);
            expect(readFile(model)==before&&!fs::exists(target),"Moved resource pre-publication recovery failed");
            journal.begin({{"A/Moved.gltf","B/Moved.gltf"}},false,false,{{fs::path("content/A/Moved.gltf"),before,after}});
            fs::rename(model,target);writeFile(target,after);
            StudioMoveJournal::recover(project);
            expect(readFile(model)==before&&!fs::exists(target),"Moved resource recovery failed");
            StudioMoveJournal::recover(project);fs::remove(model);
        }
        // v6: binary-safe moved GLB snapshot and exact pending recovery.
        {
            using namespace aiverse::studio;
            const auto model=root/"A"/"Moved.glb",target=root/"B"/"Moved.glb";
            StudioModelRepairDocument doc;doc.binary=true;doc.json=aiverse::Json::parse(R"({"images":[{"uri":"Stone.png"}]})");
            studioGlbAppendU32(doc.tail,4);studioGlbAppendU32(doc.tail,0x004e4942);doc.tail.append("\xff\0\x80\x01",4);
            const auto before=studioEncodeModelRepair(doc);
            auto images=doc.json["images"];auto image=images[0];image.set("uri",aiverse::Json::string("../A/Stone.png"));images.at(0)=std::move(image);doc.json.set("images",std::move(images));
            const auto after=studioEncodeModelRepair(doc);
            writeFile(model,before);
            StudioMoveJournal journal(project);
            journal.begin({{"A/Moved.glb","B/Moved.glb"}},false,false,{{fs::path("content/A/Moved.glb"),before,after}});
            fs::rename(model,target);writeFile(target,after);
            const auto pending=project/".aiverse-cache"/"studio-move-journal"/"pending.json";
            const auto manifest=aiverse::Json::parse(readFile(pending));
            expect(manifest.getInt("version")==8&&manifest["resources"][0].getStr("encoding")=="hex","GLB journal lacks binary encoding");
            StudioMoveJournal::recover(project);
            expect(readFile(model)==before&&!fs::exists(target),"Binary resource exact recovery failed");
            StudioMoveJournal::recover(project);fs::remove(model);
        }
        // v8: interruption after compact GLB publication at the moved path.
        {
            using namespace aiverse::studio;
            const auto model=root/"A"/"Stream.glb",target=root/"B"/"Stream.glb";
            auto doc=aiverse::Json::parse(R"({"images":[{"uri":"Stone.png"}]})");
            std::string tail;studioGlbAppendU32(tail,4);studioGlbAppendU32(tail,0x004e4942);tail.append("\xff\0\x80\x01",4);
            const auto before=studioEncodeGlbPrefix(doc,tail.size());
            writeFile(model,before+tail);
            const auto snapshot=studioReadGlbPrefix(model);
            auto images=doc["images"];auto image=images[0];image.set("uri",aiverse::Json::string("../A/Stone.png"));images.at(0)=std::move(image);doc.set("images",std::move(images));
            const auto after=studioEncodeGlbPrefix(doc,tail.size());
            aiverse::editor::ProjectFileEdit edit{fs::path("content/A/Stream.glb"),before,after,snapshot.digest,snapshot.tailBytes};
            StudioMoveJournal journal(project);
            journal.begin({{"A/Stream.glb","B/Stream.glb"}},false,false,{edit});
            fs::rename(model,target);edit.path=fs::canonical(target);
            journal.applyResources({edit});
            expect(readFile(target)==after+tail,"Streamed GLB publication changed tail");
            StudioMoveJournal::recover(project);
            expect(readFile(model)==before+tail&&!fs::exists(target),"Compact GLB rollback failed");
            StudioMoveJournal::recover(project);fs::remove(model);
        }
        // Commit marker forbids rollback on restart.
        {
            StudioMoveJournal journal(project);
            journal.begin(operations);
            fs::rename(original,moved);
            fs::rename(originalMeta,movedMeta);
            writeFile(materials,materialAfter);
            journal.commit();
            StudioMoveJournal::recover(project);
            expect(!fs::exists(original),"Committed file was rolled back");
            expect(fs::exists(moved),"Committed file missing");
            expect(fs::exists(movedMeta),"Committed sidecar missing");
            expect(readFile(materials)==materialAfter,"Committed material was rolled back");
        }

        // Unexpected external creation at source during pending move must
        // halt recovery without overwriting either copy.
        {
            StudioMoveJournal journal(project);
            journal.begin({{"B/Stone.png","A/Stone.png"}});
            fs::rename(moved,original);
            writeFile(moved,"external-edit");
            bool rejected=false;
            try { StudioMoveJournal::recover(project); }
            catch (const std::runtime_error&) { rejected=true; }
            expect(rejected,"Recovery silently overwrote external edit");
            expect(readFile(moved)=="external-edit","External edit lost");
            expect(readFile(original)=="pixels","Original file lost");
        }
        // A malformed later journal entry must be rejected before any
        // preceding item is rolled back. Reuse a fresh valid fixture.
        fs::remove_all(project/".aiverse-cache"/"studio-move-journal");
        fs::remove(moved);
        fs::rename(original,moved);
        {
            StudioMoveJournal journal(project);
            journal.begin({{"B/Stone.png","A/Stone.png"}});
            // Pending manifest describes a valid first entry and an invalid
            // second one. Prevalidation must leave both file locations intact.
            const auto pending=project/".aiverse-cache"/"studio-move-journal"/"pending.json";
            writeFile(pending,R"({"version":1,"materialExisted":true,"operations":[{"from":"B/Stone.png","to":"A/Stone.png"},{"from":"../escape","to":"A/other.png"}]})");
            bool invalidRejected=false;
            try { StudioMoveJournal::recover(project); }
            catch (const std::runtime_error&) {invalidRejected=true;}
            expect(invalidRejected,"Malformed later journal entry was accepted");
            expect(fs::exists(moved),"Malformed journal mutated the valid source");
            expect(!fs::exists(original),"Malformed journal partially restored the target");
        }
        fs::remove_all(project/".aiverse-cache"/"studio-move-journal");
        const std::string trash=".aiverse-cache/studio-asset-trash/0123456789abcdef01234567/asset";
        fs::create_directories((project/trash).parent_path());
        {
            StudioMoveJournal journal(project);
            journal.begin({{"content/B/Stone.png",trash},{"content/B/Stone.png.meta",trash+".meta"}},true);
            fs::rename(moved,project/trash);
            writeFile(materials,"external-material-change");
            StudioMoveJournal::recover(project);
            expect(fs::exists(moved)&&fs::exists(movedMeta),"Quarantine partial rollback failed");
            expect(readFile(materials)=="external-material-change","Quarantine recovery modified unrelated materials");
        }
        // v3 replacement: the same live path is used by two ordered moves.
        const std::string oldSlot=".aiverse-cache/studio-asset-trash/111111111111111111111111/asset";
        const std::string newSlot=".aiverse-cache/studio-asset-trash/222222222222222222222222/asset";
        fs::create_directories((project/oldSlot).parent_path());
        fs::create_directories((project/newSlot).parent_path());
        writeFile(project/newSlot,"replacement");
        const std::vector<StudioMoveJournalOperation> replacement{
            {"content/B/Stone.png",oldSlot,aiverse::studio::studioAssetFileDigest(moved)},
            {newSlot,"content/B/Stone.png",aiverse::studio::studioAssetFileDigest(project/newSlot)}
        };
        for(int phase=0;phase<3;++phase){
            StudioMoveJournal journal(project);journal.begin(replacement,true,true);
            if(phase>=1)fs::rename(moved,project/oldSlot);
            if(phase>=2)fs::rename(project/newSlot,moved);
            StudioMoveJournal::recover(project);
            expect(readFile(moved)=="pixels","Replacement old bytes not restored");
            expect(readFile(project/newSlot)=="replacement","Replacement staging bytes lost");
            expect(!fs::exists(project/oldSlot),"Replacement outgoing slot not restored");
            expect(readFile(materials)=="external-material-change","Replacement recovery changed materials");
        }
        // Recovery refuses a changed outgoing payload before touching any path.
        {
            StudioMoveJournal journal(project);journal.begin(replacement,true,true);
            fs::rename(moved,project/oldSlot);
            fs::rename(project/newSlot,moved);
            writeFile(moved,"external");
            bool rejected=false;
            try{StudioMoveJournal::recover(project);}catch(const std::runtime_error&){rejected=true;}
            expect(rejected&&readFile(moved)=="external","Replacement recovery overwrote external bytes");
            writeFile(moved,"replacement");
            StudioMoveJournal::recover(project);
            expect(readFile(moved)=="pixels","Replacement retry failed");
        }
        fs::remove_all(project);
        std::cout<<"PASS studio persistent move recovery and collision safety\n";
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(project,ignored);
        throw;
    }
}
