#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VXFS_HEADER "VXFS"

typedef struct 
{
    char Header[4];
    uint64_t BytesPerSector;
    uint64_t TotalSectors;

    uint32_t InodeSize;
    uint32_t InodeTableSize;
    uint8_t InodeTables;

    uint32_t DirEntryBaseSize;
    uint32_t DirTablesSize;
    uint8_t DirTables;

    uint32_t ExtentSize;
    uint32_t ExtentTableSize;
    uint32_t ExtentTables;

    uint64_t NextExtentID;
    uint64_t NextInodeID;
    uint64_t NextDirEntryID;

    uint64_t FreeInodes;
    uint64_t NextFreeInode;
    uint64_t NextFreeSector;

    uint64_t RootInodeID;

    uint64_t InodeTablesStart;
    uint64_t DirTableStart;
    uint64_t ExtentTableStart;

    uint64_t DataRegionStart;

    uint32_t InodesPerTable;


    char Label[16];

    uint8_t Unused[4];

    uint8_t padding[512-168];

}__attribute__((packed))VXFS_SUPERBLOCK;

typedef struct
{
    uint64_t StartSector;
    uint64_t SizeInSectors;

    uint16_t NextExtentID;
    uint16_t NextExtentTableID;

    uint16_t ExtentID;
    uint16_t ExtentTableID;
    uint8_t Availible;
    uint8_t NextExtentUsed;

    uint8_t padding[6];
} __attribute__((packed)) VXFS_EXTENT;

typedef struct 
{
    uint16_t InodeID;
    uint16_t InodeTableID;
    uint16_t Flags;
    uint16_t Permissions;
    uint16_t ExtentID;
    uint16_t ExtentTableID;

    uint8_t Type;

    uint8_t padding[3];
}__attribute((packed))VXFS_INODE;

int main(int argc,char* argv[])
{
    if (argc < 2)
    {
        printf("Syntax: %s <image> <args>!!!\n");
        return 1;
    }

    char* Label = NULL;
    size_t length = 0;
    if (argc > 2)
    {
        for (int i = 0; i < argc; i++)
        {
            if (!strncmp("-L",argv[i],"2"))
            {
                Label = (char*)malloc(strlen(argv[++i])+1);
                strncpy(Label,argv[i],strlen(argv[i]) + 1);
                length = strlen(argv[i]) + 1;
            }
        }
    }
    FILE* image = fopen(argv[1],"wb+");
    size_t PartitionSize = 0;
    fseek(image,0,SEEK_END);
    PartitionSize = ftell(image) / 512;
    fseek(image,0,SEEK_SET);

    VXFS_SUPERBLOCK superblock = {
        .Header = VXFS_HEADER,
        .Label = "NO NAME"
    };

    if (Label != NULL)
    {
        memset(superblock.Label,0,16);
        strncpy(superblock.Label,Label,length);
    }

    superblock.BytesPerSector = 512;
    superblock.TotalSectors = PartitionSize;
    superblock.RootInodeID = 1;
    
    //Inode Tables
    superblock.InodeTables = PartitionSize / 4096; // 1 table per 2 MiB
    superblock.InodesPerTable = PartitionSize / 32; // 1 inode per 16 KiB
    superblock.InodeTableSize = superblock.InodesPerTable * sizeof(VXFS_INODE);


    return 0;
}