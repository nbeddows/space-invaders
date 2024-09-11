/*
Copyright (c) 2021-2024 Nicolas Beddows <nicolas.beddows@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef RPIOCONTROLLER_H
#define RPIOCONTROLLER_H

#include <ArduinoJson.h>
#include <pico/util/queue.h>
#include <vector>

#include "meen_hw/MH_Factory.h"
#include "i8080_arcade/MemoryController.h"

namespace i8080_arcade
{
    /** Custom SDL io controller.

        A custom io controller targetting Space Invaders i8080 arcade hardware compatible ROMs.
    */
    class RPIoController final : public MachEmu::IController
    {
    private:
	int64_t currTime_{};

        /** i8080_arcade

            The hardware emulator.
        */
        std::unique_ptr<meen_hw::MH_II8080ArcadeIO> i8080ArcadeIO_;

        /** i8080 arcade memory

            Holds the underlying memory and vram frame pool.
        */
        std::shared_ptr<MemoryController> memoryController_;

        /** videoFrameQueue

            A single element queue used to pass the current frame to be rendered between cores.
        */
        queue_t videoFrameQueue_;

        /** Compressed video frame pool

            Video frames that are ready to be rendered.
        */
        //std::vector<meen_hw::MH_ResourcePool<std::array<uint8_t, 7168>>::ResourcePtr> videoFrames_;

        /** Mutex for thread safe video frame access

            Allows safe access to the underlying resource pool video frames.
        */
        meen_hw::MH_Mutex videoFrameMutex_;

        /** VideoFrameWrapper

            A convenience wrapper used to pass unique pointers through RP2040s C based queue api.
        */
        struct VideoFrameWrapper
        {
            meen_hw::MH_ResourcePool<std::array<uint8_t, 7168>>::ResourcePtr videoFrame;
        };

        /** videoFrameWrapper_

            A resourcePtr wrapper for use with RP2040s C based queue api

            @remark    only using one frame
        */
        VideoFrameWrapper videoFrameWrapper_;

    public:
        /** Initialisation constructor

            Creates an SDL specific i8080 arcade IO controller.
        */
        RPIoController(const std::shared_ptr<MemoryController>& memoryController, const JsonVariant& audioHardware, const JsonVariant& videoHardware);

        /** Destructor

            Free the various required SDL objects.
        */
        ~RPIoController();

        /** IController Read override

            Sample the keyboard so the CPU can take any required action.

            @param  port    The device to read from.

            @return int             A bitfield indicating the action to take.
        */
        uint8_t Read(uint16_t port) final;

        /** IController write override

            Write the relevant audio sample to the output audio device.

            @param  port    The output device to write to.
            @param  data    A bitfield indicating what data to write.
        */
        void Write(uint16_t port, uint8_t data) final;

        /** IController::ServiceInterrupts override

            Render the video ram texture to the window via the rendering context.

            @param  currTime        The current CPU run time in nanoseconds.
            @param  cycles          The number of CPU cycles completed.
        */
        MachEmu::ISR ServiceInterrupts(uint64_t currTime, uint64_t cycles) final;

        /** Uuid

            Unique universal identifier for this controller.

            @return    The uuid as a 16 byte array.
        */
        std::array<uint8_t, 16> Uuid() const final;

        /** Main control loop

            Process all incoming events.

            Events include audio/video rendering, keyboard processing and window close.
        */
        void EventLoop();

    };
} // namespace i8080_arcade
#endif // RPIOCONTROLLER_H
