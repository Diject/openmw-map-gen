local ui = require("openmw.ui")
local util = require("openmw.util")
local async = require("openmw.async")
local core = require("openmw.core")
local input = require("openmw.input")

local borders = require("scripts.map_extractor.borders")

local screenSize = ui.layers[ui.layers.indexOf("HUD")].size


ui.layers.insertAfter("MainMenuBackground", "builtin:map_extractor", {interactive = true})


local function button(text, size, position, anchor, callback)
    local layout = {
        type = ui.TYPE.Widget,
        props = {
            size = size,
            anchor = anchor,
            position = position,
        },
        events = {
            mousePress = async:callback(function(e, layout)
                if callback then callback(layout) end
            end),
        },
        content = ui.content {
            {
                type = ui.TYPE.Text,
                props = {
                    text = text,
                    textSize = 20,
                    autoSize = false,
                    size = size,
                    textColor = util.color.rgb(1, 1, 1),
                    textAlignH = ui.ALIGNMENT.Center,
                    textAlignV = ui.ALIGNMENT.Center,
                    multiline = true,
                    wordWrap = true,
                },
            },
            borders(),
        }
    }
    return layout
end


local function textLine()
    return {
        type = ui.TYPE.TextEdit,
        props = {
            text = "",
            textSize = 20,
            autoSize = true,
            textColor = util.color.rgb(1, 1, 1),
            textAlignH = ui.ALIGNMENT.Center,
            textAlignV = ui.ALIGNMENT.Center,
            textShadow = true,
            textShadowColor = util.color.rgb(0, 0, 0),
            anchor = util.vector2(0.5, 0.5),
            size = util.vector2(screenSize.x, 0),
            multiline = true,
            wordWrap = true,
            readOnly = true,
        },
    }
end


local btnPanel

local function yesBtnCallback()
    core.sendGlobalEvent("builtin:map_extractor:yesBtn")
    btnPanel.props.visible = false
    ui._setUiModeStack({})
end

local function noBtnCallback()
    core.sendGlobalEvent("builtin:map_extractor:noBtn")
    btnPanel.props.visible = false
    ui._setUiModeStack({})
end

btnPanel = {
    type = ui.TYPE.Widget,
    props = {
        size = util.vector2(screenSize.x, 40),
        visible = false,
    },
    content = ui.content {
        button("Yes (Enter/A)", util.vector2(140, 40), util.vector2(screenSize.x / 2, 0), util.vector2(1.25, 0), yesBtnCallback),
        button("No (Esc/B)", util.vector2(140, 40), util.vector2(screenSize.x / 2, 0), util.vector2(-0.25, 0), noBtnCallback),
    }
}


local content = ui.content{
    textLine(),
    textLine(),
    textLine(),
    btnPanel,
}


local textLayout = {
    type = ui.TYPE.Container,
    props = {
        anchor = util.vector2(0.5, 0.5),
        relativePosition = util.vector2(0.5, 0.5),
    },
    content = ui.content{
        {
            type = ui.TYPE.Flex,
            props = {
                align = ui.ALIGNMENT.Center,
                arrange = ui.ALIGNMENT.Center,
            },
            content = content,
        }
    },
}


local layout = {
    type = ui.TYPE.Widget,
    layer = "builtin:map_extractor",
    props = {
        anchor = util.vector2(0.5, 0.5),
        relativePosition = util.vector2(0.5, 0.5),
        size = screenSize,
    },
    content = ui.content{
        {
            type = ui.TYPE.Image,
            props = {
                resource = ui.texture{ path = "white" },
                relativeSize = util.vector2(1, 1),
                color = util.color.rgb(0, 0, 0),
                alpha = 0.9,
            }
        },
        textLayout,
    },
}


local menu = ui.create(layout)


return {
    engineHandlers = {
        onKeyPress = function (key)
            if not btnPanel.props.visible then return end

            if key.code == input.KEY.Escape or key.code == input.KEY.B then
                noBtnCallback()
            elseif key.code == input.KEY.Enter or key.code == input.KEY.A then
                yesBtnCallback()
            end
        end,

        onControllerButtonPress = function (id)
            if not btnPanel.props.visible then return end

            if id == input.CONTROLLER_BUTTON.B then
                noBtnCallback()
            elseif id == input.CONTROLLER_BUTTON.A then
                yesBtnCallback()
            end
        end,
    },

    eventHandlers = {
        ["builtin:map_extractor:updateMenu"] = function (data)
            if not data then data = {} end

            if data.line1 then
                content[1].props.text = data.line1
            end
            if data.line2 then
                content[2].props.text = data.line2
            end
            if data.line3 then
                content[3].props.text = data.line3
            end
            if data.btnVisibility ~= nil then
                btnPanel.props.visible = data.btnVisibility
                if data.btnVisibility then
                    ui._setWindowDisabled("Journal", true)
                    ui._setUiModeStack({"Journal"})
                else
                    ui._setUiModeStack({})
                end
            end

            menu:update()
        end,
    }
}