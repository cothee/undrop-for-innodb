#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>


int dirlist(const char* dir_path, char * files[], char * name[]) {
  DIR* dir = NULL;
  struct dirent *ptr;
  int i = 0;

  if ((dir = opendir(dir_path)) == NULL) {
    printf("--open dir error...");
    exit(1);
  }

  while ((ptr = readdir(dir)) != NULL) {
      if(strcmp(ptr->d_name,".") == 0 
          || strcmp(ptr->d_name,"..") == 0
          || ptr->d_type != 8) {
          continue;
      }
      char *p = ptr->d_name + strlen(ptr->d_name);
      p -= 4;
     
      if (p > ptr->d_name 
          && strcmp(p, ".ibd") == 0
          && name
          && name[i]) {
          memset(files[i], '\0', sizeof(files[i]));
          strcpy(files[i], dir_path);
          strcat(files[i], "/");
          strcat(files[i], ptr->d_name);
         // printf("%s : ", files[i]);
          // get table name
          
          strncpy(name[i], ptr->d_name, strlen(ptr->d_name) - 4);
          strcat(name[i], "\0");
           //   printf("%s\n", name[i]);
          i++;
      }

  } 
  closedir(dir);
  return i;
}

int get_primary_page(const char* dir_path, char * file) {
  DIR* dir = NULL;
  struct dirent *ptr;
  int i = 0;

  if ((dir = opendir(dir_path)) == NULL) {
    printf("--open dir error...");
    exit(1);
  }

  while ((ptr = readdir(dir)) != NULL) {
      if(strcmp(ptr->d_name,".") == 0 
          || strcmp(ptr->d_name,"..") == 0
          || ptr->d_type != 8) {
          continue;
      }
      if (file) {     
        memset(file, '\0', sizeof(file));
        strcpy(file, dir_path);
        strcat(file, "/");
        strcat(file, ptr->d_name);
        strcat(file, "\0");
        //printf("%s : ", file);

        i++;
        break;
      }
  } 
  closedir(dir);  
  return 0;
}

int rm_dir(const char* dir_path) {
  DIR* dir = NULL;
  struct stat dir_stat;
  struct dirent *ptr;
  char child[256] = "";

  if (0 > stat(dir_path, &dir_stat)) {
      printf("rm_dir: get dir stat error\n");
      return -1;
  }

  if (S_ISREG(dir_stat.st_mode)) {
      //printf("remove file: %s\n", dir_path);
      remove(dir_path);
  } else if (S_ISDIR(dir_stat.st_mode)) {
      if ((dir = opendir(dir_path)) == NULL) {
          printf("rm_dir: open dir error...\n");
          return -1;
      }

      while ((ptr = readdir(dir)) != NULL) {
          if(strcmp(ptr->d_name,".") == 0 
              || strcmp(ptr->d_name,"..") == 0) {
              continue;
          }
      
          memset(child, '\0', sizeof(child));
          strcpy(child, dir_path);
          strcat(child, "/");
          strcat(child, ptr->d_name);
          rm_dir(child);
      }
      closedir(dir);
      rmdir(dir_path);
      //printf("remove dir: %s\n", dir_path);
  }

  return 0;
}


/*
int main(){
  int i = 0;
  char path[1024] = "test";
  char page[1024] = "";
  char* name[2048];
  char* file[2048];
  for (i = 0; i < 2048; i++) {
      file[i] = (char*)malloc(sizeof(char)*1024);
      name[i] = (char*)malloc(sizeof(char)*1024);
  }
  dirlist(path, file, name);
  //get_primary_page(path, page);
  //rm_dir("output_temp");
  for (i = 0; i < 2048; i++) {
      free(file[i]);
      free(name[i]);
  }
}
*/
