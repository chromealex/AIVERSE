# Controller transitions (#162)

Scope: initial offline and network slots, authoritative departure/resume, deterministic event, replay, snapshots and legacy save migration. Verify in combined acceptance.

Implementation b8ed3dbf64ab474488cfa7386b0115e79becb373, city integration 42d15ccc325844699f8cbd6a89120ed0a691a66e, command range aaaca015ebbf47190e374bfd2bbc6afdfd8dc268. State lives in engine/sim/{Command.h,Simulation.h,Simulation.cpp}; engine/game/{GameCore.cpp,Script.cpp,ScriptApi.cpp,ScriptEngine.cpp,Components.h} apply and expose it. engine/net/server/ServerMatch.cpp records slot-zero control commands in confirmed history; engine/net/city/CityNet.{cpp,h} tracks accepted seats at simulation ticks. Existing default seat assignments keep their save/checksum encoding. Nondefault ownership writes save v11. Validation deferred: offline seat setup, legacy and v11 saves, prediction rollback, disconnect/resume, terminal tick delivery, city, replay and Lua callback.
