local core = require("openmw.core")

local this = {}

this.timers = {}
this.nextTimerId = 0
this.tick = 0


function this.new(duration, callback, ...)
    local timerId = this.nextTimerId
    this.nextTimerId = timerId + 1

    local timer = {
        endTime = this.tick + duration,
        callback = callback,
        args = {...},
    }
    this.timers[timerId] = timer
    return function ()
        this.timers[timerId] = nil
    end
end


function this.updateTimers()
    this.tick = this.tick + 1
    for i, timer in pairs(this.timers) do
        if this.tick > timer.endTime then
            timer.callback(table.unpack(timer.args))
            this.timers[i] = nil
        end
    end
end


return this