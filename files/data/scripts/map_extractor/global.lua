local util = require("openmw.util")
local world = require("openmw.world")
local async = require("openmw.async")
local core = require("openmw.core")
local types = require("openmw.types")
local realTimer = require("scripts.map_extractor.realTimer")

local visitedCells = {}

local cellCount = #world.cells
local i = cellCount
local lastTimestamp = core.getRealTime() - 50
local timeFromLast = 50
local onlyPlayerCell = true

local step = 0

local function getExCellId(gridX, gridY)
    return string.format("(%d,%d)", gridX, gridY)
end


local function getCellId(cell)
    if not cell then return end
    if cell.isExterior then
        return getExCellId(cell.gridX, cell.gridY)
    else
        return (cell.id or ""):gsub(":", "")
    end
end


local function showCompletionMessage()
    if world.isMapExtractionActive() then
        realTimer.new(1, showCompletionMessage)
        return
    end

    local pl = world.players[1]
    pl:sendEvent("builtin:map_extractor:updateMenu", {
        line1 = "Map extraction complete.",
        line2 = "",
        line3 = "",
    })
end


local function generateTilemap()
    if world.isMapExtractionActive() then
        realTimer.new(1, generateTilemap)
        return
    end

    local pl = world.players[1]
    pl:sendEvent("builtin:map_extractor:updateMenu", {
        line1 = "Generating tile world map...",
        line2 = "The game may freeze for a while.",
        line3 = "",
    })

    realTimer.new(1, function ()
        world.generateTileWorldMap(util.color.rgb(0.255, 0.243, 0.212))
        showCompletionMessage()
    end)
end


local function processAndTeleport(skipExtraction)
    local pl = world.players[1]

    if not skipExtraction then
        world.extractLocalMaps(onlyPlayerCell)
    end

    local function func()
        if world.isMapExtractionActive() then
            realTimer.new(0, func)
            return
        elseif skipExtraction then
            pl:sendEvent("builtin:map_extractor:updateMenu", {
                line1 = "Generating local maps...",
            })
        end

        repeat
            if i % 50 == 0 then
                local currentTime = core.getRealTime()
                timeFromLast = (timeFromLast + currentTime - lastTimestamp) / 2
                lastTimestamp = currentTime
            end

            local res, cell = pcall(function ()
                return world.cells[i]
            end)
            if not res then cell = nil end
            i = i - 1

            local pos

            local customCellId = getCellId(cell)
            if not cell or not customCellId or visitedCells[customCellId] then goto continue end

            visitedCells[customCellId] = true
            if not onlyPlayerCell and cell.isExterior then
                for j = cell.gridX - 1, cell.gridX + 1 do
                    for k = cell.gridY - 1, cell.gridY + 1 do
                        visitedCells[getExCellId(j, k)] = true
                    end
                end
            end

            if cell.isExterior then
                pos = util.vector3(cell.gridX * 8192 + 4096, cell.gridY * 8192 + 4096, 0)
            else
                pos = util.vector3(0, 0, 0)
            end

            do
                local estimatedTimeLeft = math.max(timeFromLast, 1) / 50 * i
                local hours = math.floor(estimatedTimeLeft / 3600)
                estimatedTimeLeft = estimatedTimeLeft % 3600
                local minutes = math.floor(estimatedTimeLeft / 60)
                local seconds = estimatedTimeLeft % 60
                pl:sendEvent("builtin:map_extractor:updateMenu", {
                    line2 = string.format("Processed %d / %d cells", cellCount - i, cellCount),
                    line3 = string.format("Estimated time left: %d:%02d:%02.0f", hours, minutes, seconds),
                })

                print(string.format('Teleporting to cell #%d: "%s"', i, customCellId))
                pl:teleport(cell, pos)

                break
            end

            ::continue::
        until i <= 0

        if i <= 0 then
            generateTilemap()
        end
    end

    realTimer.new(1, func)
end


local function start()
    step = 4
    local pl = world.players[1]
    pl:sendEvent("builtin:map_extractor:updateMenu", {line1 = "Generating world map...", btnVisibility = false})
    world.enableExtractionMode()

    realTimer.new(0, function ()
        local parametes = world.getLaunchParameters()
        local pixPerCell = tonumber(parametes["world-map-pixelsPerCell"]) or 32
        local borderSize = tonumber(parametes["world-map-border"]) or 2
        local disableWaterAlpha = parametes["world-map-disable-water-alpha"] == "true"

        world.extractWorldMap(pixPerCell, borderSize, not disableWaterAlpha)
        if not world.getOverwriteFlag() then
            for _, cellId in pairs(world.getExistingLocalMapIds() or {}) do
                visitedCells[cellId] = true
            end
        end

        processAndTeleport(true)
    end)
end


local function doStep()
    local advancedMapDir = world.getContentFileDir("Advanced World Map.omwscripts")
    if step == 0 and advancedMapDir then
        step = 1
        world.players[1]:sendEvent("builtin:map_extractor:updateMenu", {
            line1 = "Found \"Advanced World Map\" mod. Would you like to extract map images to its directory?",
            line2 = "", line3 = "",
            btnVisibility = true,
        })

    elseif step <= 1 and world.getLaunchParameters()["clear-output-dirs"] ~= "true" and
            next(world.getExistingLocalMapIds() or {}) then
        step = 2
        world.players[1]:sendEvent("builtin:map_extractor:updateMenu", {
            line1 = "Existing local map textures detected. Do you want to delete them? (Recommended)",
            line2 = "", line3 = "",
            btnVisibility = true,
        })
    elseif step <= 2 and world.getLaunchParameters()["overwrite-maps"] ~= "true" and
            next(world.getExistingLocalMapIds() or {}) then
        step = 3
        world.players[1]:sendEvent("builtin:map_extractor:updateMenu", {
            line1 = "Existing local map textures detected. Do you want to overwrite them?",
            line2 = "", line3 = "",
            btnVisibility = true,
        })
    else
        start()
    end
end


async:newUnsavableSimulationTimer(0.1, function ()
    world.pause("main")
    local pl = world.players[1]
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.Controls, false)
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.Fighting, false)
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.Jumping, false)
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.Looking, false)
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.Magic, false)
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.VanityMode, false)
    types.Player.setControlSwitch(pl, types.Player.CONTROL_SWITCH.ViewMode, false)

    doStep()
end)



return {
    engineHandlers = {
        onUpdate = function(dt)
            realTimer.updateTimers()
        end,
    },
    eventHandlers = {
        ["builtin:map_extractor:teleport"] = function (pl)
            processAndTeleport()
        end,

        ["builtin:map_extractor:yesBtn"] = function ()
            if step == 1 then
                local advancedMapDir = world.getContentFileDir("Advanced World Map.omwscripts")
                if advancedMapDir then
                    world.setWorldMapOutputPath(advancedMapDir .. "/textures/advanced_world_map/custom/")
                    world.setLocalMapOutputPath(advancedMapDir .. "/textures/advanced_world_map/local/")
                end
                doStep()
            elseif step == 2 then
                world.clearMapOutputDirs()
                doStep()
            elseif step == 3 then
                world.setLaunchParameter("overwrite-maps", true)
                start()
            end
        end,

        ["builtin:map_extractor:noBtn"] = function ()
            if step == 1 then
                doStep()
            elseif step == 2 then
                doStep()
            elseif step == 3 then
                start()
            end
        end,
    }
}