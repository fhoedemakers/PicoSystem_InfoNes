#ifndef ROMLISTER
#define ROMLISTER
#include <string>
#include <vector>
#include "rom_selector.h" 
#define ROMLISTER_MAXPATH 100
namespace Frens {

	class RomLister
	{

	public:
		struct RomEntry {
			char Path[ROMLISTER_MAXPATH];  // Without dirname
			bool IsDirectory = false;
			int Index = 0;
		};
		RomLister(uintptr_t NES_FILE_ADDR,  void *buffer, size_t buffersize);
		~RomLister();
		RomEntry* GetEntries();
		char  *FolderName();
		size_t Count();
		void list( );

	private:
		char directoryname[ROMLISTER_MAXPATH];
		int length{};
		size_t max_entries{};
		RomEntry *entries{};
		size_t numberOfEntries{};
		const uint8_t *address{};
		

	};
}
#endif
