#include "apps/editor/core/WorldOperatorJobs.h"
#include "tests/AlwaysCheck.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace aiverse;
using namespace aiverse::editor;
int main(){
    WorldOperatorJobs jobs;
    auto wait=[&](uint64_t id){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        for(;;){
            auto result=jobs.status(id);
            if(result.getStr("state")!="running")return result;
            AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()<deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    auto id=jobs.start("/project-a","inspect",[](auto& control){
        control.progress("reading");
        while(!control.cancelled())std::this_thread::yield();
        control.check();return Json::object();
    });
    AIVERSE_TEST_CHECK(jobs.occupied()&&!jobs.release(id));
    bool busy=false;try{jobs.start("/project-b","other",[](auto&){return Json();});}catch(...){busy=true;}
    AIVERSE_TEST_CHECK(busy&&jobs.cancel(id));
    AIVERSE_TEST_CHECK(wait(id).getStr("state")=="cancelled"&&jobs.release(id));
    std::atomic<bool> entered=false,finish=false;
    struct Release{std::atomic<bool>& flag;~Release(){flag=true;}} guard{finish};
    id=jobs.start("/project-b","checkpoint",[&](auto& control){
        control.irreversible("writing");entered=true;
        while(!finish.load())std::this_thread::yield();
        Json j=Json::object();j.set("durable",Json::fromBool(true));return j;
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!entered.load()){
        AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()<deadline);
        std::this_thread::yield();
    }
    AIVERSE_TEST_CHECK(!jobs.cancel(id)&&!jobs.release(id));
    finish=true;
    const auto result=wait(id);
    AIVERSE_TEST_CHECK(result.getStr("state")=="succeeded"&&result["result"].getBool("durable"));
    AIVERSE_TEST_CHECK(result.getStr("project")=="/project-b"&&jobs.release(id));
    id=jobs.start("/project-b","failure",[](auto&)->Json{throw std::runtime_error("fixture failure");});
    AIVERSE_TEST_CHECK(wait(id).getStr("state")=="failed"&&jobs.release(id));
    AIVERSE_TEST_CHECK(jobs.status().getStr("state")=="idle");
}
