/////////////////////////////////////////////////////////////////
//                                                             //
//         Trabalho II: Sistema de Gest�o de Ficheiros         //
//                                                             //
// compila��o: gcc vfs.c -Wall -lreadline -lcurses -o vfs      //
// utiliza��o: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM //
//                                                             //
/////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <readline/readline.h>
#include <readline/history.h>

#define DEBUG 1

#define MAXARGS 100
#define CHECK_NUMBER 9999
#define TYPE_DIR 'D'
#define TYPE_FILE 'F'
#define MAX_NAME_LENGHT 20

#define FAT_ENTRIES(TYPE) (TYPE == 8 ? 256 : TYPE == 10 ? 1024 : 4096)
#define FAT_SIZE(TYPE) (FAT_ENTRIES(TYPE) * sizeof(int))
#define BLOCK(N) (blocks + N * sb->block_size)
#define DIR_ENTRIES_PER_BLOCK (sb->block_size / sizeof(dir_entry))

typedef struct command {
  char *cmd;              // string apenas com o comando
  int argc;               // n�mero de argumentos
  char *argv[MAXARGS+1];  // vector de argumentos do comando
} COMMAND;

typedef struct superblock_entry {
  int check_number;   // n�mero que permite identificar o sistema como v�lido
  int block_size;     // tamanho de um bloco {256, 512(default) ou 1024 bytes}
  int fat_type;       // tipo de FAT {8, 10(default) ou 12}
  int root_block;     // n�mero do 1� bloco a que corresponde o direct�rio raiz
  int free_block;     // n�mero do 1� bloco da lista de blocos n�o utilizados
  int n_free_blocks;  // total de blocos n�o utilizados
} superblock;

typedef struct directory_entry {
  char type;                   // tipo da entrada (TYPE_DIR ou TYPE_FILE)
  char name[MAX_NAME_LENGHT];  // nome da entrada
  unsigned char day;           // dia em que foi criada (entre 1 e 31)
  unsigned char month;         // mes em que foi criada (entre 1 e 12)
  unsigned char year;          // ano em que foi criada (entre 0 e 255 - 0 representa o ano de 1900)
  int size;                    // tamanho em bytes (0 se TYPE_DIR)
  int first_block;             // primeiro bloco de dados
} dir_entry;

// vari�veis globais
superblock *sb;   // superblock do sistema de ficheiros
int *fat;         // apontador para a FAT
char *blocks;     // apontador para a regi�o dos dados
int current_dir;  // bloco do direct�rio corrente

// fun��es auxiliares
COMMAND parse(char*);
void parse_argv(int, char*[]);
void init_filesystem(int, int, char*);
void init_superblock(int, int);
void init_fat(void);
void init_dir_block(int, int);
void init_dir_entry(dir_entry*, char, char*, int, int);
void exec_com(COMMAND);

// fun��es de manipula��o de direct�rios
void vfs_ls(void);
void vfs_mkdir(char*);
void vfs_cd(char*);
void vfs_pwd(void);
void vfs_rmdir(char*);

// fun��es de manipula��o de ficheiros
void vfs_get(char*, char*);
void vfs_put(char*, char*);
void vfs_cat(char*);
void vfs_cp(char*, char*);
void vfs_mv(char*, char*);
void vfs_rm(char*);


int main(int argc, char *argv[]) {
  char *linha;
  COMMAND com;

  parse_argv(argc, argv);
  while (1) {
    if ((linha = readline("vfs$ ")) == NULL)
      exit(0);
    if (strlen(linha) != 0) {
      add_history(linha);
      com = parse(linha);
      exec_com(com);
    }
    free(linha);
  }
  return 0;
}


COMMAND parse(char *linha) {
  int i = 0;
  COMMAND com;

  com.cmd = strtok(linha, " ");
  com.argv[0] = com.cmd;
  while ((com.argv[++i] = strtok(NULL, " ")) != NULL);
  com.argc = i;
  return com;
}


void parse_argv(int argc, char *argv[]) {
  int i, block_size, fat_type;

  block_size = 512; // valor por omiss�o
  fat_type = 10;    // valor por omiss�o
  if (argc < 2 || argc > 4) {
    printf("vfs: invalid number of arguments\n");
    printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
    exit(1);
  }
  for (i = 1; i < argc - 1; i++) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'b') {
	block_size = atoi(&argv[i][2]);
	if (block_size != 256 && block_size != 512 && block_size != 1024) {
	  printf("vfs: invalid block size (%d)\n", block_size);
	  printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
	  exit(1);
	}
      } else if (argv[i][1] == 'f') {
	fat_type = atoi(&argv[i][2]);
	if (fat_type != 8 && fat_type != 10 && fat_type != 12) {
	  printf("vfs: invalid fat type (%d)\n", fat_type);
	  printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
	  exit(1);
	}
      } else {
	printf("vfs: invalid argument (%s)\n", argv[i]);
	printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
	exit(1);
      }
    } else {
      printf("vfs: invalid argument (%s)\n", argv[i]);
      printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
      exit(1);
    }
  }
  init_filesystem(block_size, fat_type, argv[argc-1]);
  return;
}


void init_filesystem(int block_size, int fat_type, char *filesystem_name) {
  int fsd, filesystem_size;

  if ((fsd = open(filesystem_name, O_RDWR)) == -1) {
    // o sistema de ficheiros n�o existe --> � necess�rio cri�-lo e format�-lo
    if ((fsd = open(filesystem_name, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU)) == -1) {
      printf("vfs: cannot create filesystem (%s)\n", filesystem_name);
      printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
      exit(1);
    }

    // calcula o tamanho do sistema de ficheiros
    filesystem_size = block_size + FAT_SIZE(fat_type) + FAT_ENTRIES(fat_type) * block_size;
    printf("vfs: formatting virtual file-system (%d bytes) ... please wait\n", filesystem_size);

    // estende o sistema de ficheiros para o tamanho desejado
    lseek(fsd, filesystem_size - 1, SEEK_SET);
    write(fsd, "", 1);

    // faz o mapeamento do sistema de ficheiros e inicia as vari�veis globais
    if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      printf("vfs: cannot map filesystem (mmap error)\n");
      close(fsd);
      exit(1);
    }
    fat = (int *) ((unsigned long int) sb + block_size);
    blocks = (char *) ((unsigned long int) fat + FAT_SIZE(fat_type));
    
    // inicia o superblock
    init_superblock(block_size, fat_type);
    
    // inicia a FAT
    init_fat();
    
    // inicia o bloco do direct�rio raiz '/'
    init_dir_block(sb->root_block, sb->root_block);
  } else {
    // calcula o tamanho do sistema de ficheiros
    struct stat buf;
    stat(filesystem_name, &buf);
    filesystem_size = buf.st_size;

    // faz o mapeamento do sistema de ficheiros e inicia as vari�veis globais
    if ((sb = (superblock *) mmap(NULL, filesystem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fsd, 0)) == MAP_FAILED) {
      printf("vfs: cannot map filesystem (mmap error)\n");
      close(fsd);
      exit(1);
    }
    fat = (int *) ((unsigned long int) sb + sb->block_size);
    blocks = (char *) ((unsigned long int) fat + FAT_SIZE(sb->fat_type));

    // testa se o sistema de ficheiros � v�lido 
    if (sb->check_number != CHECK_NUMBER || filesystem_size != sb->block_size + FAT_SIZE(sb->fat_type) + FAT_ENTRIES(sb->fat_type) * sb->block_size) {
      printf("vfs: invalid filesystem (%s)\n", filesystem_name);
      printf("Usage: vfs [-b[256|512|1024]] [-f[8|10|12]] FILESYSTEM\n");
      munmap(sb, filesystem_size);
      close(fsd);
      exit(1);
    }
  }
  close(fsd);

  // inicia o direct�rio corrente
  current_dir = sb->root_block;
  return;
}


void init_superblock(int block_size, int fat_type) {
  sb->check_number = CHECK_NUMBER;
  sb->block_size = block_size;
  sb->fat_type = fat_type;
  sb->root_block = 0;
  sb->free_block = 1;
  sb->n_free_blocks = FAT_ENTRIES(fat_type) - 1;
  return;
}


void init_fat(void) {
  int i;

  fat[0] = -1;
  for (i = 1; i < sb->n_free_blocks; i++)
    fat[i] = i + 1;
  fat[sb->n_free_blocks] = -1;
  return;
}


void init_dir_block(int block, int parent_block) {
  dir_entry *dir = (dir_entry *) BLOCK(block);
  // o n�mero de entradas no direct�rio (inicialmente 2) fica guardado no campo size da entrada "."
  init_dir_entry(&dir[0], TYPE_DIR, ".", 2, block);
  init_dir_entry(&dir[1], TYPE_DIR, "..", 0, parent_block);
  return;
}


void init_dir_entry(dir_entry *dir, char type, char *name, int size, int first_block) {
  time_t cur_time = time(NULL);
  struct tm *cur_tm = localtime(&cur_time);

  dir->type = type;
  strcpy(dir->name, name);
  dir->day = cur_tm->tm_mday;
  dir->month = cur_tm->tm_mon + 1;
  dir->year = cur_tm->tm_year;
  dir->size = size;
  dir->first_block = first_block;
  return;
}

int cstr_cmp(const void *a, const void *b) 
{ 
  const char **ia = (const char **)a;
  const char **ib = (const char **)b;
  return strcmp(*ia, *ib);
} 

int get_free_block() {
  if (sb->n_free_blocks == 0)
    return -1;

  int livre = sb->free_block;
  sb->free_block = fat[livre];
  fat[livre] = -1;

  sb->n_free_blocks--;

  return livre;
}

void exec_com(COMMAND com) {
  // para cada comando invocar a fun��o que o implementa
  if (!strcmp(com.cmd, "exit"))
    exit(0);
  if (!strcmp(com.cmd, "ls")) {
    // falta tratamento de erros
    vfs_ls();
  } else if (!strcmp(com.cmd, "mkdir")) {
    // falta tratamento de erros
    vfs_mkdir(com.argv[1]);
  } else if (!strcmp(com.cmd, "cd")) {
    // falta tratamento de erros
    vfs_cd(com.argv[1]);
  } else if (!strcmp(com.cmd, "pwd")) {
    // falta tratamento de erros
    vfs_pwd();
  } else if (!strcmp(com.cmd, "rmdir")) {
    // falta tratamento de erros
    vfs_rmdir(com.argv[1]);
  } else if (!strcmp(com.cmd, "get")) {
    // falta tratamento de erros
    vfs_get(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "put")) {
    // falta tratamento de erros
    vfs_put(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "cat")) {
    // falta tratamento de erros
    vfs_cat(com.argv[1]);
  } else if (!strcmp(com.cmd, "cp")) {
    // falta tratamento de erros
    vfs_cp(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "mv")) {
    // falta tratamento de erros
    vfs_mv(com.argv[1], com.argv[2]);
  } else if (!strcmp(com.cmd, "rm")) {
    // falta tratamento de erros
    vfs_rm(com.argv[1]);
  } else
    printf("ERROR(input: command not found)\n");
  return;
}


// ls - lista o conte�do do direct�rio actual
void vfs_ls(void) {
  dir_entry *dir = (dir_entry *) BLOCK(current_dir);
  int n_entries = dir[0].size, i;

  char type_str[100];
  char **content = (char **) malloc(n_entries * sizeof(char *));
  for (i = 0; i < n_entries; i++)
    content[i] = (char * ) malloc(1024 * sizeof(char *));
  

  int cur_block = current_dir;
  for (i = 0; i < n_entries; i++)
  {
    if (i % DIR_ENTRIES_PER_BLOCK == 0 && i)
    {
      if (DEBUG)
        printf("Changed Block\n");
      cur_block = fat[cur_block];
      dir = (dir_entry *) BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;

    if (dir[block_i].type == TYPE_DIR)
      sprintf(type_str, "DIR");
    else
      sprintf(type_str, "%d", dir[block_i].size);
    
    sprintf(content[i], "%s\t%02d-%02d-%04d\t%s", dir[block_i].name, dir[block_i].day, dir[block_i].month, 1900 + dir[block_i].year, type_str);
  }

  qsort(content, n_entries, sizeof(char *), cstr_cmp);

  for (i = 0; i < n_entries; i++)
    printf("%s\n", content[i]);

  return;
}


// mkdir dir - cria um subdirect�rio com nome dir no direct�rio actual
void vfs_mkdir(char *nome_dir) {
  dir_entry *dir = (dir_entry *) BLOCK(current_dir);
  int n_entries = dir[0].size;
  int req_blocks = (n_entries % DIR_ENTRIES_PER_BLOCK == 0) + 1;

  if (sb->n_free_blocks < req_blocks)
  {
    printf("ERROR(mkdir: memory full)\n");
    return;
  }

  if (DEBUG)
    printf("Blocks: used %d from %lu\n", n_entries + 1, DIR_ENTRIES_PER_BLOCK);

  dir[0].size++;

  int new_block = get_free_block();
  init_dir_block(new_block, current_dir);

  int cur_block = current_dir;
  while (fat[cur_block] != -1)
    cur_block = fat[cur_block];

  if (n_entries % DIR_ENTRIES_PER_BLOCK == 0)
  {
    new_block = get_free_block();
    fat[cur_block] = new_block;
    cur_block = new_block;
  }

  dir = (dir_entry *) BLOCK(cur_block);
  
  init_dir_entry(&dir[n_entries % DIR_ENTRIES_PER_BLOCK], TYPE_DIR, nome_dir, 0, new_block);

  return;
}


// cd dir - move o direct�rio actual para dir.
void vfs_cd(char *nome_dir) {
  dir_entry *dir = (dir_entry *) BLOCK(current_dir);
  int n_entries = dir[0].size, i;

  int cur_block = current_dir;
  for (i = 0; i < n_entries; i++)
  {
    if (i % DIR_ENTRIES_PER_BLOCK == 0 && i)
    {
      if (DEBUG)
        printf("Changed Block\n");
      cur_block = fat[cur_block];
      dir = (dir_entry *) BLOCK(cur_block);
    }

    int block_i = i % DIR_ENTRIES_PER_BLOCK;
        
    if (dir[block_i].type == TYPE_DIR && strcmp(dir[block_i].name, nome_dir) == 0)
    {
      current_dir = dir[block_i].first_block;
      return;
    }
  }

  printf("ERROR(input: directory not found)\n");

  return;
}


// pwd - escreve o caminho absoluto do direct�rio actual
void vfs_pwd(void) {
  char name[1024], tmp[1024];
  name[0] = '/';
  name[1] = '\0';

  int it_dir = current_dir;
  int i;
  while (it_dir != 0)
  {
    dir_entry *dir = (dir_entry *) BLOCK(it_dir);
    int prev_dir = dir[1].first_block;
    dir = (dir_entry *) BLOCK(prev_dir);
    int n_entries = dir[0].size;
    
    for (i = 0; i < n_entries; i++)
      if (dir[i].first_block == it_dir)
      {
	strcpy(tmp, dir[i].name);
	strcat(tmp, name);
	strcpy(name, tmp);
	strcpy(tmp, "/");
	strcat(tmp, name);
	strcpy(name, tmp);
	break;
      }

    it_dir = prev_dir;
  }

  printf("%s\n", name);

  return;
}


// rmdir dir - remove o subdirect�rio dir (se vazio) do direct�rio actual
void vfs_rmdir(char *nome_dir) {
  dir_entry *dir = (dir_entry *) BLOCK(current_dir);
  int n_entries = dir[0].size;

  int i;
  for (i = 0; i < n_entries; i++)
    if (dir[i].type == TYPE_DIR && strcmp(dir[i].name, nome_dir) == 0)
    {
      dir_entry *del_dir = (dir_entry *) BLOCK(dir[i].first_block);

      if (del_dir[0].size != 2)
      {
	printf("ERROR(input: directory not empty)\n");
	return;
      }

      dir[i].type = dir[n_entries - 1].type;
      strcpy(dir[i].name, dir[n_entries - 1].name);
      dir[i].day = dir[n_entries - 1].day;
      dir[i].month = dir[n_entries - 1].month;
      dir[i].year = dir[n_entries - 1].year;
      dir[i].size = dir[n_entries - 1].size;
      dir[i].first_block = dir[n_entries - 1].first_block;
      dir[0].size--;

      return;
    }

  printf("ERROR(input: directory not found)\n");

  return;
}


// get fich1 fich2 - copia um ficheiro normal UNIX fich1 para um ficheiro no nosso sistema fich2
void vfs_get(char *nome_orig, char *nome_dest) {
  return;
}


// put fich1 fich2 - copia um ficheiro do nosso sistema fich1 para um ficheiro normal UNIX fich2
void vfs_put(char *nome_orig, char *nome_dest) {
  return;
}


// cat fich - escreve para o ecr� o conte�do do ficheiro fich
void vfs_cat(char *nome_fich) {
  return;
}


// cp fich1 fich2 - copia o ficheiro fich1 para fich2
// cp fich dir - copia o ficheiro fich para o subdirect�rio dir
void vfs_cp(char *nome_orig, char *nome_dest) {
  return;
}


// mv fich1 fich2 - move o ficheiro fich1 para fich2
// mv fich dir - move o ficheiro fich para o subdirect�rio dir
void vfs_mv(char *nome_orig, char *nome_dest) {
  return;
}


// rm fich - remove o ficheiro fich
void vfs_rm(char *nome_fich) {
  return;
}
