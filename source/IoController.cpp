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

#include <assert.h>
#include <bitset>
#include <cstring>

#include "SpaceInvaders/IoController.h"

namespace SpaceInvaders
{
    IoController::IoController(const std::shared_ptr<MemoryController>& memoryController)
		: memoryController_{ memoryController }
	{

	}

	uint8_t IoController::ReadFrom(uint16_t port)
	{
		if (port == 3)
		{
			return (shiftData_ >> (8 - shiftAmount_)) & 0xFF;
		}
		else if (port == 0)
		{
			return 0;
		}

		return 0;
	}

	uint8_t IoController::WriteTo(uint16_t port, uint8_t data)
	{
		std::bitset<8> audio = 0;

		if (port == 2)
		{
			//Writing to port 2 (bits 0, 1, 2) sets the offset for the 8 bit result
			shiftAmount_ = data & 0x07; //we are only interested in the first 3 bits
		}
		else if (port == 3)
		{
			// Ufo audio repeats, so we'll handle that as a separate case
			audio[0] = (data & 1) | (port3Byte_ & 1);

			for (int i = 1; i < 8; i++)
			{
				audio[i] = (data & (1 << i)) > (port3Byte_ & (1 << i));
			}

			port3Byte_ = data;
		}
		else if (port == 4)
		{
			shiftData_ = (shiftData_ >> 8) | (static_cast<uint16_t>(data) << 8);
		}
		else if (port == 5)
		{
			for (int i = 0; i < 8; i++)
			{
				audio[i] = (data & (1 << i)) > (port5Byte_ & (1 << i));
			}

			port5Byte_ = data;
		}
		else if (port == 6)
		{
			//printf("Watch-dog: %d\n", data);
		}
		else
		{
			// Force a failure
			assert(port >= 2 && port <= 6);
			//printf("Unknown device: %d\n", data);
		}

		return audio.to_ulong();
	}

	MachEmu::ISR IoController::ServiceInterrupts(uint64_t currTime, uint64_t cycles)
	{
		auto isr = MachEmu::ISR::NoInterrupt;

		if (quit_ == false)
		{
			if (currTime != lastTime_)
			{
				isr = nextInterrupt_;

				//Check last interrupt, if it is One then we are at the start of the vBlank
				if (isr == MachEmu::ISR::One)
				{
					//Signal vBlank interrupt.
					nextInterrupt_ = MachEmu::ISR::Two;
				}
				else
				{
					//Signal that the 'crt beam' is about half was down the screen.
					nextInterrupt_ = MachEmu::ISR::One;

					std::lock_guard<std::mutex> lock(mutex_);
					memcpy(vram_.data(), memoryController_->GetVram().get(), vram_.size());
				}

				lastTime_ = currTime;
			}
		}
		else
		{
			isr = MachEmu::ISR::Quit;
		}

		return isr;
	}

	void IoController::Blit(uint8_t* texture, uint8_t rowBytes)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto vramStart = vram_.data();
		auto vramEnd = vramStart + memoryController_->GetVramLength();
		int8_t shift = 0;
		//Since we are decompressing the video ram, we will also perform the
		//required 270 degree rotation.
		auto start = texture + rowBytes * (memoryController_->GetScreenHeight() - 1);
		auto ptr = texture;

		while (vramStart < vramEnd)
		{
			//Decompress the vram from 1bpp to 8bpp.
			*ptr = ((*vramStart >> shift) & 0x01) * 0xFF; // 0xFF - The 8 bit colour to decompress to, in this case white, but it could be anything within the 8 bit range.
			//Cycle the shift value between 0-7.
			shift = ++shift & 0x07;
			//Move to the next vram byte if we have done a full cycle.
			vramStart += shift == 0;
			//If we are not at the end, move to the next row, otherwise move to the next column.
			ptr - rowBytes >= texture ? ptr -= rowBytes : ptr = ++start;
		}
	}
} // namespace SpaceInvaders