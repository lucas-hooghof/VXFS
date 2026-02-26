#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../VXFS.h"

int main(int argc,char* argv[])
{
    if (argc < 2)
    {
        printf("Syntax: %s <image> <args>!!!\n",argv[0]);
        return 1;
    }

    char* Label = NULL;
    size_t length = 0;
    if (argc > 2)
    {
        for (int i = 0; i < argc; i++)
        {
            if (!strncmp("-L",argv[i],2))
            {
                Label = (char*)malloc(strlen(argv[++i])+1);
                strncpy(Label,argv[i],strlen(argv[i]) + 1);
                length = strlen(argv[i]) + 1;
            }
        }
    }
    FILE* image = fopen(argv[1],"rb+");
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
    superblock.InodeTables = PartitionSize / GROUP_SIZE_SECTORS;

    superblock.InodesPerTable =
        GROUP_SIZE_SECTORS / INODE_RATIO_SECTORS;  // 128

    superblock.InodeTableSize =
        (superblock.InodesPerTable * sizeof(VXFS_INODE) + 511) / 512;

    superblock.NextInodeID = 2;
    superblock.InodeTablesStart = 1;


    //Extent Tables
    superblock.ExtentTables = superblock.InodeTables;

    superblock.ExtentsPerTable =
        superblock.InodesPerTable * EXTENTS_PER_INODE;  // 512

    superblock.ExtentTableSize =
        (superblock.ExtentsPerTable * sizeof(VXFS_EXTENT) + 511) / 512;

    superblock.ExtentTableStart = superblock.InodeTablesStart + (superblock.InodeTables * superblock.InodeTableSize);


    superblock.DataRegionStart = superblock.ExtentTableStart + (superblock.ExtentTables * superblock.ExtentTableSize);

    fseek(image,0,SEEK_SET);
    if (fwrite(&superblock,1,sizeof superblock,image) != sizeof superblock)
    {
        fprintf(stderr, "Failed to write superblock\n");
        return 1;
    }

    //Inode Table Init
    fseek(image,superblock.InodeTablesStart * 512,SEEK_SET);
    for (size_t table = 0; table < superblock.InodeTables; table++)
    {
        for (size_t inodeid = 0; inodeid < superblock.InodesPerTable; inodeid++)
        {
            VXFS_INODE inode = {0};
            inode.InodeID = inodeid;
            inode.InodeTableID = table;
            inode.free = inodeid == 0 ? 0 : 1;
            if (fwrite(&inode,1,sizeof inode,image) != sizeof inode)
            {
                fprintf(stderr,"Failed to write Inode\n");
                return 1;
            }
        }
    }

    fseek(image,superblock.ExtentTableStart * 512,SEEK_SET);
    for (size_t table = 0; table < superblock.ExtentTables; table++)
    {
        for (size_t ExtentId = 0; ExtentId < superblock.ExtentsPerTable; ExtentId++)
        {
            VXFS_EXTENT extent = {0};
            extent.Availible = 1;
            extent.ExtentID = ExtentId;
            extent.ExtentTableID = table;
            extent.NextExtentUsed = 0;
        }
    }

    fseek(image,superblock.InodeTablesStart * 512 + sizeof(VXFS_INODE),SEEK_SET);
    VXFS_INODE inode = {0};
    fread(&inode,1,sizeof inode,image);
    inode.free = 0;
    inode.Flags = DIR | SYSTEM;
    inode.ExtentID = 0;
    inode.ExtentTableID = 0;
    inode.SizeInBytes = sizeof(VXFS_DIRENTRY) * 2 + 2 +3;
    fseek(image,superblock.InodeTablesStart * 512 + sizeof(VXFS_INODE),SEEK_SET);
    if (fwrite(&inode,1,sizeof inode,image) != sizeof inode)
    {
        fprintf(stderr,"Failed to write root inode\n");
        return 1;
    }
    fseek(image,superblock.ExtentTableStart * 512,SEEK_SET);
    VXFS_EXTENT rootextent = {0};
    fread(&rootextent,1,sizeof rootextent,image);
    rootextent.Availible = 0;
    rootextent.StartSector = superblock.DataRegionStart;
    rootextent.SizeInSectors = 1;
    fseek(image,superblock.ExtentTableStart * 512,SEEK_SET);
    if (fwrite(&rootextent,1,sizeof rootextent,image) != sizeof rootextent)
    {
        fprintf(stderr,"Failed to write root Extent\n");
        return 1;
    }

    fseek(image,rootextent.StartSector * 512,SEEK_SET);
    VXFS_DIRENTRY dotentry = {0};
    dotentry.InodeID = inode.InodeID;
    dotentry.InodeTableID = inode.InodeTableID;
    dotentry.NameLenght = 2;
    printf(". location: %ld\n",ftell(image));
    if (fwrite(&dotentry,1,sizeof dotentry,image) != sizeof dotentry)
    {
        fprintf(stderr,"Failed to write the root . direntry\n");
        return 1;
    }
    char dot[2] = ".";
    if (fwrite(&dot,1,sizeof dot,image) != sizeof dot)
    {  
        fprintf(stderr,"Failed to write root . entry filename\n");
        return 1;
    }

    dotentry.NameLenght = 3;
    char dotdot[3] = "..";
    printf(".. location: %ld\n",ftell(image));
    if (fwrite(&dotentry,1,sizeof dotentry,image) != sizeof dotentry)
    {
        fprintf(stderr,"Failed to write root .. direntry\n");
        return 1;
    }
    if (fwrite(&dotdot,1,sizeof dotdot,image) != sizeof dotdot)
    {
        fprintf(stderr,"Failed to write root .. entry filename\n");
        return 1;
    } 



    return 0;
}