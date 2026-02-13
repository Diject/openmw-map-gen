#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/formatversion.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/lua/configuration.hpp>
#include <components/lua/serialization.hpp>
#include <components/testing/expecterror.hpp>
#include <components/testing/util.hpp>

namespace
{

    using testing::ElementsAre;
    using testing::Pair;

    std::vector<std::pair<int, std::string>> asVector(const LuaUtil::ScriptIdsWithInitializationData& d)
    {
        std::vector<std::pair<int, std::string>> res;
        for (const auto& [k, v] : d)
            res.emplace_back(k, std::string(v));
        return res;
    }

    TEST(LuaConfigurationTest, ValidOMWScripts)
    {
        ESM::LuaScriptsCfg cfg;
        LuaUtil::parseOMWScripts(cfg, R"X(
            # Lines starting with '#' are comments
            GLOBAL:  my_mod/#some_global_script.lua

            # Script that will be automatically attached to the player
            PLAYER :my_mod/player.lua
            CUSTOM : my_mod/some_other_script.lua
            NPC , CREATURE PLAYER : my_mod/some_other_script.lua)X");
        LuaUtil::parseOMWScripts(cfg, ":my_mod/player.LUA  \r\nCREATURE,CUSTOM: my_mod/creature.lua\r\n");

        ASSERT_EQ(cfg.mScripts.size(), 6);
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[0]), "GLOBAL : my_mod/#some_global_script.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[1]), "PLAYER : my_mod/player.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[2]), "CUSTOM : my_mod/some_other_script.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[3]), "PLAYER NPC CREATURE : my_mod/some_other_script.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[4]), ": my_mod/player.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(cfg.mScripts[5]), "CUSTOM CREATURE : my_mod/creature.lua");

        LuaUtil::ScriptsConfiguration conf;
        conf.init(std::move(cfg));
        ASSERT_EQ(conf.size(), 4);
        EXPECT_EQ(LuaUtil::scriptCfgToString(conf[0]), "GLOBAL : my_mod/#some_global_script.lua");
        // cfg.mScripts[1] is overridden by cfg.mScripts[4]
        // cfg.mScripts[2] is overridden by cfg.mScripts[3]
        EXPECT_EQ(LuaUtil::scriptCfgToString(conf[1]), "PLAYER NPC CREATURE : my_mod/some_other_script.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(conf[2]), ": my_mod/player.lua");
        EXPECT_EQ(LuaUtil::scriptCfgToString(conf[3]), "CUSTOM CREATURE : my_mod/creature.lua");

        EXPECT_THAT(asVector(conf.getGlobalConf()), ElementsAre(Pair(0, "")));
        EXPECT_THAT(asVector(conf.getPlayerConf()), ElementsAre(Pair(1, "")));
        const ESM::RefId something = ESM::RefId::stringRefId("something");

        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_CONT, something, ESM::RefNum())), ElementsAre());
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_NPC_, something, ESM::RefNum())), ElementsAre(Pair(1, "")));
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_CREA, something, ESM::RefNum())),
            ElementsAre(Pair(1, ""), Pair(3, "")));

        // Check that initialization cleans old data
        cfg = ESM::LuaScriptsCfg();
        conf.init(std::move(cfg));
        EXPECT_EQ(conf.size(), 0);
    }

    TEST(LuaConfigurationTest, InvalidOMWScripts)
    {
        ESM::LuaScriptsCfg cfg;
        EXPECT_ERROR(LuaUtil::parseOMWScripts(cfg, "GLOBAL: something"),
            "Lua script should have suffix '.lua', got: GLOBAL: something");
        EXPECT_ERROR(LuaUtil::parseOMWScripts(cfg, "something.lua"), "No flags found in: something.lua");

        cfg.mScripts.clear();
        EXPECT_NO_THROW(LuaUtil::parseOMWScripts(cfg, "GLOBAL, PLAYER: something.lua"));
        LuaUtil::ScriptsConfiguration conf;
        EXPECT_ERROR(conf.init(std::move(cfg)), "Global script can not have local flags");
    }

    TEST(LuaConfigurationTest, ConfInit)
    {
        ESM::LuaScriptsCfg cfg;
        ESM::LuaScriptCfg& script1 = cfg.mScripts.emplace_back();
        script1.mScriptPath = VFS::Path::Normalized("Script1.lua");
        script1.mInitializationData = "data1";
        script1.mFlags = ESM::LuaScriptCfg::sPlayer;
        script1.mTypes.push_back(ESM::REC_CREA);
        script1.mRecords.push_back({ true, ESM::RefId::stringRefId("record1"), "dataRecord1" });
        script1.mRefs.push_back({ true, 2, 3, "" });
        script1.mRefs.push_back({ true, 2, 4, "" });

        ESM::LuaScriptCfg& script2 = cfg.mScripts.emplace_back();
        script2.mScriptPath = VFS::Path::Normalized("Script2.lua");
        script2.mFlags = ESM::LuaScriptCfg::sCustom;
        script2.mTypes.push_back(ESM::REC_CONT);

        ESM::LuaScriptCfg& script1Extra = cfg.mScripts.emplace_back();
        script1Extra.mScriptPath = VFS::Path::Normalized("script1.LUA");
        script1Extra.mFlags = ESM::LuaScriptCfg::sCustom | ESM::LuaScriptCfg::sMerge;
        script1Extra.mTypes.push_back(ESM::REC_NPC_);
        script1Extra.mRecords.push_back({ false, ESM::RefId::stringRefId("rat"), "" });
        script1Extra.mRecords.push_back({ true, ESM::RefId::stringRefId("record2"), "" });
        script1Extra.mRefs.push_back({ true, 3, 5, "dataRef35" });
        script1Extra.mRefs.push_back({ false, 2, 3, "" });

        LuaUtil::ScriptsConfiguration conf;
        conf.init(cfg);
        ASSERT_EQ(conf.size(), 2);
        EXPECT_EQ(LuaUtil::scriptCfgToString(conf[0]),
            "CUSTOM PLAYER CREATURE NPC : script1.lua ; data 5 bytes ; 3 records ; 4 objects");
        EXPECT_EQ(LuaUtil::scriptCfgToString(conf[1]), "CUSTOM CONTAINER : script2.lua");

        EXPECT_THAT(asVector(conf.getPlayerConf()), ElementsAre(Pair(0, "data1")));
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_CONT, ESM::RefId::stringRefId("something"), ESM::RefNum())),
            ElementsAre(Pair(1, "")));
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_CREA, ESM::RefId::stringRefId("guar"), ESM::RefNum())),
            ElementsAre(Pair(0, "data1")));
        EXPECT_THAT(
            asVector(conf.getLocalConf(ESM::REC_CREA, ESM::RefId::stringRefId("rat"), ESM::RefNum())), ElementsAre());
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_DOOR, ESM::RefId::stringRefId("record1"), ESM::RefNum())),
            ElementsAre(Pair(0, "dataRecord1")));
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_DOOR, ESM::RefId::stringRefId("record2"), ESM::RefNum())),
            ElementsAre(Pair(0, "data1")));
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_NPC_, ESM::RefId::stringRefId("record3"), { 1, 1 })),
            ElementsAre(Pair(0, "data1")));
        EXPECT_THAT(
            asVector(conf.getLocalConf(ESM::REC_NPC_, ESM::RefId::stringRefId("record3"), { 2, 3 })), ElementsAre());
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_NPC_, ESM::RefId::stringRefId("record3"), { 3, 5 })),
            ElementsAre(Pair(0, "dataRef35")));
        EXPECT_THAT(asVector(conf.getLocalConf(ESM::REC_CONT, ESM::RefId::stringRefId("record4"), { 2, 4 })),
            ElementsAre(Pair(0, "data1"), Pair(1, "")));

        ESM::LuaScriptCfg& script3 = cfg.mScripts.emplace_back();
        script3.mScriptPath = VFS::Path::Normalized("script1.lua");
        script3.mFlags = ESM::LuaScriptCfg::sGlobal;
        EXPECT_ERROR(conf.init(cfg), "Flags mismatch for script1.lua");
    }
}
