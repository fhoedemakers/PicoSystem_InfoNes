#include <stdio.h>
#include <pico/stdlib.h>
#include <string.h>
#include "RomLister.h"
#include "FrensHelpers.h"
#include "tar.h"

inline bool checkNESMagic(const uint8_t *data);
// class to listing directories and .NES files 
namespace Frens
{
	

	// Buffer must have sufficient bytes to contain directory contents
	RomLister::RomLister(uintptr_t NES_FILE_ADDR, void *buffer, size_t buffersize)
	{
		entries = (RomEntry *)buffer;
		max_entries = buffersize / sizeof(RomEntry);
		address = reinterpret_cast<const uint8_t *>(NES_FILE_ADDR);
		numberOfEntries = 0;
	}

	RomLister::~RomLister()
	{
	}

	RomLister::RomEntry *RomLister::GetEntries()
	{
		return entries;
	}

	char *RomLister::FolderName()
	{
		return directoryname;
	}
	size_t RomLister::Count()
	{
		return numberOfEntries;
	}
	// implementation of the compare function for qsort ignore case
	int compareRomEntry(const void *a, const void *b)
	{
		const RomLister::RomEntry *entryA = (const RomLister::RomEntry *)a;
		const RomLister::RomEntry *entryB = (const RomLister::RomEntry *)b;
		return strcasecmp(entryA->Path, entryB->Path);
	}
	
	
	void RomLister::list( )
	{
		numberOfEntries = GetValidTAREntries(address, checkNESMagic);
		for ( int i=0; i< numberOfEntries; i++) {
			entries[i].Index = i;
			entries[i].IsDirectory = false;
			strcpy(entries[i].Path, extractTAREntryatindex(i, address, checkNESMagic).filename.data());
		}
		// sort the entries by Path
		qsort(entries, numberOfEntries, sizeof(RomEntry), compareRomEntry);
		
	}
	
}
