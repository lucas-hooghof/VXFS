#include <stdint.h>

#define VXFS_HEADER "VXFS"

#define GROUP_SIZE_SECTORS   4096   // 2 MiB
#define INODE_RATIO_SECTORS  32     // 16 KiB per inode
#define EXTENTS_PER_INODE    4

#define BIT(x) (1 << x)

typedef struct 
{
    char Header[4];
    uint64_t BytesPerSector;
    uint64_t TotalSectors;

    uint32_t InodeSize;
    uint32_t InodeTableSize;
    uint8_t InodeTables;


    uint32_t ExtentSize;
    uint32_t ExtentTableSize;
    uint32_t ExtentTables;

    uint64_t NextExtentID;
    uint64_t NextInodeID;


    uint64_t FreeInodes;
    uint64_t NextFreeInode;
    uint64_t NextFreeSector;

    uint64_t RootInodeID;

    uint64_t InodeTablesStart;
    uint64_t ExtentTableStart;

    uint64_t DataRegionStart;

    uint16_t InodesPerTable;
    uint16_t ExtentsPerTable;

    char Label[16];

    uint8_t Unused[2];

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

    uint8_t free;
    uint64_t SizeInBytes;
    uint8_t padding[3];
}__attribute((packed))VXFS_INODE;

typedef struct
{
    uint16_t InodeID;
    uint16_t InodeTableID;
    uint16_t padding;
    uint16_t NameLenght;
    //Follows Name length but not in struct because it can vary
}__attribute((packed))VXFS_DIRENTRY;

typedef enum
{
    DIR = BIT(0),
    SYSTEM = BIT(1),
    SYSLINK = BIT(2),
    HARDLINK = BIT(3)
}VXFS_FLAGS;