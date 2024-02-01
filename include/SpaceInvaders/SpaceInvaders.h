/*
Copyright (c) 2021-2023 Nicolas Beddows <nicolas.beddows@gmail.com>

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

#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>

#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_mixer.h"
#include "Base/Base.h"
#include "Controller/IController.h"

using namespace MachEmu;

namespace SpaceInvaders
{
	/** Custom memory controller.

		A custom memory controller targetting the Space Invaders arcade ROM.
	*/
	class MemoryController final : public IController
	{
	private:
		/** Memory size.

			The size in bytes of the memory.
		*/
		//cppcheck-suppress unusedStructMember
		size_t memorySize_{};

		/** Memory buffer.

			The memory bytes that the cpu will read from and write to.
		*/
		std::unique_ptr<uint8_t[]> memory_;

	public:
		/** Contructor.

			Create a memory controller that can address memory of the
			specified address bus size. For this demo Space Invaders
			runs on an Intel8080 with 64k of memory, therefore the
			address bus size will be 16.

			@param		addressBusSize	The size of the address bus.
										This will be 16.
		*/
		explicit MemoryController(uint8_t addressBusSize);

		~MemoryController() = default;

		/** Screen width.

			Space Invaders has a width of 224 @ 1bpp.

			NOTE: this differs from the vram width which is 256.
				  (It is written to vram with a 90 degree rotation.)
		*/
		constexpr uint16_t GetScreenWidth() const { return 224; }
		
		/** Screen height.

			Space Invaders has a height of 256 @ 1bpp.

			NOTE: this differs from the vram height which is 224.
				  (It is written to vram with a 90 degree rotation.)
		*/
		constexpr uint16_t GetScreenHeight() const { return 256; }

		/** The size of the video ram.

			Space Invaders has a constant size of 7168
		*/
		static constexpr uint16_t GetVramLength() { return 7168; }

		/** Video ram.

			This is a new allocation with a copy of the video ram.

			@return		unique_ptr		The video ram.

			NOTE: The length of the video ram returned is given by:

				  GetVRAMLength()

			NOTE: This isn't the best way to do this, one should use a resource pool
				  to avoid the unnecessary allocations.
		*/
		std::unique_ptr<uint8_t[]> GetVram() const;

		/** Load ROM file.

			Loads the specified rom file and the given memory address offset.

			Space Invaders rom files have the following ROM layout:

				invaders-h 0000-07FF
				invaders-g 0800-0FFF
				invaders-f 1000-17FF
				invaders-e 1800-1FFF
		*/
		void Load(const char* romFile, uint16_t offset);

		/** Memory size.

			Returns the size of the memory, in our example it will be 64k (2^addressBusSize(16))
		*/
		size_t Size() const;

		/** Read from controller.

			Reads 8 bits of data from the specifed 16 bit memory address.

			@see IController::Read for further details.
		*/
		uint8_t Read(uint16_t address) override final;

		/** Write to controller.

			Write 8 bits of data to the specifed 16 bit memory address.

			@see IController::Write for further details.
		*/
		void Write(uint16_t address, uint8_t value) override final;

		/** Service memory interrupts.

			Memory interrupts are never generated.

			The function will always return ISR::NoInterrupt.
		*/
		ISR ServiceInterrupts(uint64_t currTime, uint64_t cycles) override final;
	};

	/** Custom io controller.

		A custom io controller targetting the Space Invaders arcade ROM.
	*/
	class IoController : public IController
	{
	private:
		/** The next interrupt to execute.

			nextInterrupt_ holds the next ISR that will be sent to the cpu.

			Space Invaders listens for two interrupts, ISR::One and ISR::Two.
			ISR::One is issued when the 'CRT beam' is near the center of the screen.
			ISR::Two is issued when the 'CRT beam' is at the end of the screen (VBLANK start).
		*/
		ISR nextInterrupt_{ ISR::One };
		
		/** Last cpu time.

			lastTime_ holds the previous cpu time that ServiceInterrupts was last called.

			In order to emulate a 60hz display we need to fire ISR::One and ISR::Two
			at 60hz intervals.
		*/
		uint64_t lastTime_{};

		/** Last cycle count.
		
			Last cycle count gets updated to the current running cycle count when we decide
			to generate an interrupt, for Space Invaders this will be 16666 cpu cycles.
		*/
		//uint64_t lastCycleCount_{};
		
		/** Dedicated Shift Hardware.

			The 8080 instruction set does not include opcodes for shifting.
			An 8-bit pixel image must be shifted into a 16-bit word for the desired bit-position on the screen.
			Space Invaders adds a hardware shift register to help with the math.

			shiftIn_, shiftAmount_ and shiftData_ help emulate this.
		*/
		//cppcheck-suppress unusedStructMember
		uint8_t shiftIn_{};
		//cppcheck-suppress unusedStructMember
		uint8_t shiftAmount_{};
		//cppcheck-suppress unusedStructMember
		uint16_t shiftData_{};

		/**	Backup port 3 and port 5 bytes.
		
			These ports are used for writing to an output audio device.
			We only want to write the audio when the required port bit
			changes from off to on, hence we need to backup these bytes
			to make that comparison.
		*/
		//cppcheck-suppress unusedStructMember
		uint8_t port3Byte_{};
		//cppcheck-suppress unusedStructMember
		uint8_t port5Byte_{};

	protected:
		/** The maximum number of output audio sample files.

			There are only 9 audio files that are used.

			@see wavFiles_
		*/
		static constexpr uint8_t totalWavFiles_ = 16;

		/** Video RAM access.

			When the video ram is ready to be blitted it is sampled
			from the memory controller at the vram address.
		*/
		std::shared_ptr<MemoryController> memoryController_;
		
		/** Exit control loop.

			A value of true will cause the Machine control loop to exit.
			This can be set, for example, when the keyboard 'q' key is pressed.
		*/
		std::atomic_bool quit_{};

		/** The audio files to use for sound effects.

			NOTE: DO NOT change the order of these files as they corrospond to the
			correct port number bits of port 3 (low 8 bits) and 5 (high 8 bits)
		*/
		std::array<const char*, totalWavFiles_> wavFiles_ 
		{
			ROMS_DIR"ufo_highpitch.wav",	/**< UFO */
			ROMS_DIR"shoot.wav",			/**< Player fire */
			ROMS_DIR"explosion.wav",		/**< Player killed */
			ROMS_DIR"invaderkilled.wav",	/**<	Invader killed */
			nullptr,						/**< Extended Play */
			nullptr,						/**< AMP Enable */
			nullptr,						/**< Unused */
			nullptr,						/**< Unused */
			ROMS_DIR"fastinvader1.wav",		/**< Invader fleet movement 1 */
			ROMS_DIR"fastinvader2.wav",		/**< Invader fleet movement 1 */
			ROMS_DIR"fastinvader3.wav",		/**< Invader fleet movement 1 */
			ROMS_DIR"fastinvader4.wav",		/**< Invader fleet movement 1 */
			ROMS_DIR"ufo_lowpitch.wav",		/**< UFO hit */
			nullptr,						/**< Unused */
			nullptr,						/**< Unused */
			nullptr							/**< Unused */
		};

		std::mutex mutex_;
		std::array<uint8_t, MemoryController::GetVramLength()> vram_;
	public:
		/** Initialisation contructor.

			Creates an io controller which has access to the memory controller for
			video ram access.

			@param		memoryController	The memory controller where the video ram resides.
		*/
		explicit IoController(const std::shared_ptr<MemoryController>& memoryController);

		/** Read from controller.

			Read the value from the input device (keyboard for example)
			and set the relevant bit in the return value according to
			the following:

			Port 0
				 bit 0 DIP4 (Seems to be self-test-request read at power up)
				 bit 1 Always 1
				 bit 2 Always 1
				 bit 3 Always 1
				 bit 4 Fire
				 bit 5 Left
				 bit 6 Right
				 bit 7 ? tied to demux port 7 ?

			Port 1
				 bit 0 = CREDIT (1 if deposit)
				 bit 1 = 2P start (1 if pressed)
				 bit 2 = 1P start (1 if pressed)
				 bit 3 = Always 1
				 bit 4 = 1P shot (1 if pressed)
				 bit 5 = 1P left (1 if pressed)
				 bit 6 = 1P right (1 if pressed)
				 bit 7 = Not connected

			Port 2
				 bit 0 = DIP3 00 = 3 ships  10 = 5 ships
				 bit 1 = DIP5 01 = 4 ships  11 = 6 ships
				 bit 2 = Tilt
				 bit 3 = DIP6 0 = extra ship at 1500, 1 = extra ship at 1000
				 bit 4 = P2 shot (1 if pressed)
				 bit 5 = P2 left (1 if pressed)
				 bit 6 = P2 right (1 if pressed)
				 bit 7 = DIP7 Coin info displayed in demo screen 0=ON

			Port 3
				bit 0-7 Shift register data

			@param		port		The input device to read from.

			@return		uint8_t		Non zero if the port was read from, zero otherwise.
		*/
		uint8_t ReadFrom(uint16_t port);

		/** Write to controller.

			Write the data to the relevant output device
			according to the following:

			Port 2:
				bit 0,1,2 Shift amount

			Port 3: (discrete sounds)
				bit 0=UFO (repeats)        SX0 0.raw
				bit 1=Shot                 SX1 1.raw
				bit 2=Flash (player die)   SX2 2.raw
				bit 3=Invader die          SX3 3.raw
				bit 4=Extended play        SX4
				bit 5= AMP enable          SX5
				bit 6= NC (not wired)
				bit 7= NC (not wired)
				Port 4: (discrete sounds)
				bit 0-7 shift data (LSB on 1st write, MSB on 2nd)

			Port 5:
				bit 0=Fleet movement 1     SX6 4.raw
				bit 1=Fleet movement 2     SX7 5.raw
				bit 2=Fleet movement 3     SX8 6.raw
				bit 3=Fleet movement 4     SX9 7.raw
				bit 4=UFO Hit              SX10 8.raw
				bit 5= NC (Cocktail mode control ... to flip screen)
				bit 6= NC (not wired)
				bit 7= NC (not wired)

			Port 6:
				Watchdog ... read or write to reset

			@param	port		The output device to write to.
			@param	data		The data to write to the output device.

			@return	uint16_t	1 if the data on the port was handled, 0 otherwise.
		*/
		std::bitset<16> WriteTo(uint16_t port, uint8_t data);

		/** Service io interrupts.

			Return ISR::One and ISR::Two at 60hz intervals.
			This informs the ROM that it is safe to draw to
			the top and bottom of the video ram.

			@return		ISR		ISR::One when the 'beam' is near the centre of the screen,
								ISR::Two when the 'beam' is at the end (vBlank). 
		*/
		ISR ServiceInterrupts(uint64_t currTime, uint64_t cycles) override;

		/** Write space invaders vram to texture.
		
			The vram is written with a 90 degree rotation, therefore it needs to be
			rotated a further 270 degrees so it can be rendered with the correct
			orientation.

			@param	texture		The video memory to write to.
			@param	rowBytes	The width of each scanline in bytes.

		*/
		void Blit(uint8_t* texture, uint8_t rowBytes);
	};

	/** Custom SDL io controller.

		A custom io controller targetting the Space Invaders arcade ROM.
	*/
	class SdlIoController final : public IoController
	{
		private:
			/** SDL Renderer.

				The window rendering context.
			*/
			//cppcheck-suppress unusedStructMember
			SDL_Renderer* renderer_{};

			/**	SDL_texture.
				
				The texture which will hold the video ram for rendering.
			*/
			//cppcheck-suppress unusedStructMember
			SDL_Texture* texture_{};

			/** SDL_Window.

				The window to draw the video ram to.
			*/
			//cppcheck-suppress unusedStructMember
			SDL_Window* window_{};
			
			/** Audio samples.
			
				The various audio samples to be played.

				@See IoController::WavFiles_
			*/
			//cppcheck-suppress unusedStructMember
			std::array<Mix_Chunk*, totalWavFiles_> mixChunk_;

			/**
				The custom Space Invaders SDL event type

				Event codes are defined in the EventCode enumeration.

				@see EventCode
			*/
			uint64_t siEvent_{};

			enum EventCode
			{
				RenderVideo,	/**< The next video frame is ready to be rendered. This event drives the control loop */
				RenderAudio,	/**< Audio is ready to be played. The siEvent data1 type is the index into the mixChunk_ to be played. */
			};

		public:
			/** Initialisation constructor.
			
				Creates an SDL specific Space Invaders IO controller.
			*/
			explicit SdlIoController(const std::shared_ptr<MemoryController>& memoryController);
			
			/** Destructor.
			
				Free the various required SDL objects.
			*/
			~SdlIoController();

			/** IController Read override.
			
				Sample the keyboard so the CPU can take any required action.
			
				@param	port	The device to read from.

				@return	int		A bitfield indicating the action to take.

				@see IoController::ReadFrom.
			*/
			uint8_t Read(uint16_t port) final;

			/** IController write override.
			
				Write the relevant audio sample to the output audio device.
			
				@param	port	The output device to write to.
				@param	data	A bitfield indicating what data to write.

				@see IoController::WriteTo.
			*/
			void Write(uint16_t port, uint8_t data) final;

			/** IController::ServiceInterrupts override.
			
				Render the video ram texture to the window via the rendering context.

				@param	currTime	The current CPU run time in nanoseconds.
				@param	cycles		The number of CPU cycles completed.

				@see IoController::ServiceInterrupts.
			*/
			ISR ServiceInterrupts(uint64_t currTime, uint64_t cycles) final;

			/**	Main control loop

				Process all incoming events.

				Events include audio/video rendering, keyboard processing and window close.
			*/
			void EventLoop();
	};
}