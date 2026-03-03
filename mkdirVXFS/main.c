#include "../VXFS.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

bool FindDir(FILE* image,size_t sectors,char* dir)
{
    VXFS_DIRENTRY direntry = {0};
    
    for (size_t i = 0; i < sectors; i++)
    {
        for (size_t blockoffset = 0; i < BLOCK_SIZE; i+= sizeof(VXFS_DIRENTRY))
        {
            fread(&direntry,1,sizeof direntry,image);
            if (!memcmp(&direntry,(uint8_t[sizeof(VXFS_DIRENTRY)]){0},sizeof(VXFS_DIRENTRY)))
            {
                return false;
            }
            
            char* name = (char*)malloc(direntry.NameLenght);
            blockoffset += fread(name,1,direntry.NameLenght,image);
            printf("Name: %s, dir: %s,pos: %ld\n",name,dir,ftell(image));
            if (strlen(name) != 0 && !strncmp(name,dir,strlen(name)))
            {
                free(name);
                return true;
            }
            free(name);
        }

    }
    return false;
}

int main(int argc,char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr,"Syntax: %s <filename> <dir can be with depth>\n",argv[0]);
        return 1;
    }

    FILE* image = fopen(argv[1],"rb+");

    VXFS_SUPERBLOCK superblock = {0};
    if (fread(&superblock,1,sizeof superblock,image) != sizeof superblock)
    {
        fprintf(stderr,"Failed to read superblock");
        return 1;
    }

    VXFS_INODE rootinode = {0};
    fseek(image,superblock.InodeTablesStart * BLOCK_SIZE + sizeof(VXFS_INODE),SEEK_SET);
    if (fread(&rootinode,1,sizeof rootinode,image) != sizeof rootinode)
    {
        fprintf(stderr,"Failed to read rootinode");
        return 1;
    }
    
    VXFS_EXTENT rootextent = {0};
    fseek(image,superblock.ExtentTableStart * BLOCK_SIZE,SEEK_SET);
    if (fread(&rootextent,1,sizeof rootextent,image) != sizeof rootextent)
    {
        fprintf(stderr,"Failed to read root extent");
        return 1;
    }

    char* start = argv[2];
    size_t depth = 0;
    char* end;
    while ((end = strchr(start,'/')) != NULL)
    {
        *end = '\0';
        start = end + 1;
        depth++;

    }

    char** dirs = (char**)malloc(depth * sizeof(char*));
    size_t i = 0;
    while ((end = strchr(start,'/')) != NULL)
    {
        *end = '\0';
        size_t size = strlen(end);
        dirs[i] = (char*)malloc(size);
        strncpy(dirs[i],end,size);
        start = end + 1;
    }


    fseek(image,rootextent.StartSector * BLOCK_SIZE,SEEK_SET);

    if (depth == 0)
    {
        char* dir = (char*)malloc(sizeof(char) * strlen(argv[2]));
        strncpy(dir,argv[2],strlen(argv[2]));
        printf("Dir: %s\n",dir);
        if (!FindDir(image,rootextent.SizeInSectors,dir))
        {
            VXFS_INODE newnode = {0};
            newnode.InodeID = superblock.NextInodeID;
            newnode.InodeTableID = superblock.NextInodeTableID;
            newnode.ExtentID = superblock.NextExtentID;
            newnode.free = 0;
            newnode.ExtentTableID = superblock.NextExtentTableID;

            newnode.Flags = DIR;

            superblock.NextExtentID++;
            superblock.NextExtentID %= superblock.ExtentsPerTable;

            if ((newnode.ExtentID % superblock.ExtentsPerTable) == 0)
            {
                superblock.NextExtentTableID++;
            }

            superblock.NextInodeID++;
            superblock.NextInodeID %= superblock.InodesPerTable;

            if ((newnode.InodeID % superblock.InodesPerTable) == 0)
            {
                superblock.NextInodeTableID++;
            }
            
            fseek(image,superblock.InodeTablesStart * BLOCK_SIZE + newnode.InodeTableID * superblock.InodeTableSize * BLOCK_SIZE + newnode.InodeID *sizeof(VXFS_INODE),SEEK_SET);
            fwrite(&newnode,1,sizeof newnode,image);

            printf("Wrote inode at %ld\n",ftell(image) - sizeof(VXFS_INODE));

            VXFS_DIRENTRY direntry = {0};
            direntry.InodeID = newnode.InodeID;
            direntry.InodeTableID = newnode.InodeTableID;
            direntry.NameLenght = strlen(dir);
            fseek(image,rootextent.StartSector * BLOCK_SIZE,SEEK_SET);
            VXFS_DIRENTRY tmpentry = {0};
            size_t blockoffset = 0;
            size_t i = 0;
            bool found = 0;
            printf("Sectors: %ld\n",rootextent.SizeInSectors);
            for (i = 0; i < rootextent.SizeInSectors; i++)
            {
                blockoffset = 0;
                for (blockoffset = 0; blockoffset < BLOCK_SIZE; blockoffset += sizeof(VXFS_DIRENTRY))
                {
                    fread(&tmpentry,1,sizeof tmpentry,image);
                    if(!memcmp(&tmpentry,(uint8_t[sizeof(VXFS_DIRENTRY)]){0},sizeof(VXFS_DIRENTRY)))
                    {
                        found = 1;
                        goto done;
                    }
                    char* name = (char*)malloc(tmpentry.NameLenght);
                    blockoffset += fread(name,1,tmpentry.NameLenght,image);
                    free(name);
                }
            }

            done:
            if (found != 1)
            {
                printf("Failed to find free block have to implement shit here tho");
                return 1;
            }
            fseek(image,rootextent.StartSector * BLOCK_SIZE + i * BLOCK_SIZE + blockoffset,SEEK_SET);
            fwrite(&direntry,1,sizeof direntry,image);
            printf("Dir: %s length: %ld\n",dir,strlen(dir));
            fwrite(dir,1,strlen(dir),image);
        }
    }
    else {
        printf("Depth: %ld\n",depth);
    }

    fclose(image);

}