#include "luascripts.hpp"

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <components/lua/luastateptr.hpp>
#include <components/lua/serialization.hpp>

// List of all records, that are related to Lua.
//
// Records:
// LUAL - LuaScriptsCfg - list of all scripts (in content files)
// LUAM - MWLua::LuaManager (in saves)
//
// Subrecords:
// LUAF - LuaScriptCfg::mFlags and ESM::RecNameInts list
// LUAW - Simulation time and last generated RefNum
// LUAE - Start of MWLua::LocalEvent or MWLua::GlobalEvent (eventName)
// LUAS - VFS path to a Lua script
// LUAD - Serialized Lua variable
// LUAT - MWLua::ScriptsContainer::Timer
// LUAC - Name of a timer callback (string)
// LUAR - Attach script to a specific record (LuaScriptCfg::PerRecordCfg)
// LUAI - Attach script to a specific instance (LuaScriptCfg::PerRefCfg)

void ESM::saveLuaBinaryData(ESMWriter& esm, const std::string& data)
{
    if (data.empty())
        return;
    esm.startSubRecord("LUAD");
    esm.write(data.data(), data.size());
    esm.endRecord("LUAD");
}

std::string ESM::loadLuaBinaryData(ESMReader& esm)
{
    std::string data;
    if (esm.isNextSub("LUAD"))
    {
        esm.getSubHeader();
        data.resize(esm.getSubSize());
        esm.getExact(data.data(), data.size());
    }
    return data;
}

static bool readBool(ESM::ESMReader& esm)
{
    char c;
    esm.getT<char>(c);
    return c != 0;
}

void ESM::LuaScriptsCfg::load(ESMReader& esm)
{
    // Skip Lua scripts from ESM files without loading them
    while (esm.isNextSub("LUAS"))
    {
        // Skip script path
        esm.skipHString();

        // Skip LUAF subrecord
        esm.getSubNameIs("LUAF");
        esm.skipHSub();

        // Skip initialization data
        loadLuaBinaryData(esm);

        // Skip LUAR records
        while (esm.isNextSub("LUAR"))
        {
            esm.skipHSub();
            loadLuaBinaryData(esm);
        }
        
        // Skip LUAI records
        while (esm.isNextSub("LUAI"))
        {
            esm.skipHSub();
            loadLuaBinaryData(esm);
        }
    }
}

void ESM::LuaScriptsCfg::adjustRefNums(const ESMReader& esm)
{
    auto adjustRefNumFn = [&esm](int contentFile) -> int {
        if (contentFile == 0)
            return esm.getIndex();
        else if (contentFile > 0 && contentFile <= static_cast<int>(esm.getParentFileIndices().size()))
            return esm.getParentFileIndices()[contentFile - 1];
        else
            throw std::runtime_error("Incorrect contentFile index");
    };

    LuaUtil::LuaStatePtr state(luaL_newstate());
    if (state == nullptr)
        throw std::runtime_error("Failed to create Lua runtime");

    LuaUtil::BasicSerializer serializer(adjustRefNumFn);

    auto adjustLuaData = [&](std::string& data) {
        if (data.empty())
            return;
        sol::object luaData = LuaUtil::deserialize(state.get(), data, &serializer);
        data = LuaUtil::serialize(luaData, &serializer);
    };

    for (LuaScriptCfg& script : mScripts)
    {
        adjustLuaData(script.mInitializationData);
        for (LuaScriptCfg::PerRecordCfg& recordCfg : script.mRecords)
            adjustLuaData(recordCfg.mInitializationData);
        for (LuaScriptCfg::PerRefCfg& refCfg : script.mRefs)
        {
            adjustLuaData(refCfg.mInitializationData);
            refCfg.mRefnumContentFile = adjustRefNumFn(refCfg.mRefnumContentFile);
        }
    }
}

void ESM::LuaScriptsCfg::save(ESMWriter& esm) const
{
    for (const LuaScriptCfg& script : mScripts)
    {
        esm.writeHNString("LUAS", script.mScriptPath);
        esm.startSubRecord("LUAF");
        esm.writeT<uint32_t>(script.mFlags);
        for (uint32_t type : script.mTypes)
            esm.writeT<uint32_t>(type);
        esm.endRecord("LUAF");
        saveLuaBinaryData(esm, script.mInitializationData);
        for (const LuaScriptCfg::PerRecordCfg& recordCfg : script.mRecords)
        {
            esm.startSubRecord("LUAR");
            esm.writeT<char>(recordCfg.mAttach ? 1 : 0);
            esm.writeHRefId(recordCfg.mRecordId);
            esm.endRecord("LUAR");
            saveLuaBinaryData(esm, recordCfg.mInitializationData);
        }
        for (const LuaScriptCfg::PerRefCfg& refCfg : script.mRefs)
        {
            esm.startSubRecord("LUAI");
            esm.writeT<char>(refCfg.mAttach ? 1 : 0);
            esm.writeT<uint32_t>(refCfg.mRefnumIndex);
            esm.writeT<int32_t>(refCfg.mRefnumContentFile);
            esm.endRecord("LUAI");
            saveLuaBinaryData(esm, refCfg.mInitializationData);
        }
    }
}

void ESM::LuaScripts::load(ESMReader& esm)
{
    // Skip Lua scripts from save files without loading them
    while (esm.isNextSub("LUAS"))
    {
        // Skip script path
        esm.skipHString();
        
        // Skip script data
        loadLuaBinaryData(esm);
        
        // Skip timers
        while (esm.isNextSub("LUAT"))
        {
            esm.skipHSub();
            
            // Skip callback name (LUAC)
            if (esm.isNextSub("LUAC"))
                esm.skipHString();
            
            // Skip callback argument
            loadLuaBinaryData(esm);
        }
    }
}

void ESM::LuaScripts::save(ESMWriter& esm) const
{
    for (const LuaScript& script : mScripts)
    {
        esm.writeHNString("LUAS", script.mScriptPath);
        saveLuaBinaryData(esm, script.mData);
        for (const LuaTimer& timer : script.mTimers)
        {
            esm.startSubRecord("LUAT");
            esm.writeT(timer.mType);
            esm.writeT(timer.mTime);
            esm.endRecord("LUAT");
            esm.writeHNString("LUAC", timer.mCallbackName);
            if (!timer.mCallbackArgument.empty())
                saveLuaBinaryData(esm, timer.mCallbackArgument);
        }
    }
}
