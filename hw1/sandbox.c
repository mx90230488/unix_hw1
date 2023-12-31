#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include<sys/mman.h>
#include <fcntl.h>
#include<dlfcn.h>
#include<errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
static int (*real_open)(const char *, int,mode_t);
static ssize_t (*real_read)(int , void *, size_t );
static ssize_t (*real_write)(int , const void* , size_t );
static int (*real_connect)(int , const struct sockaddr *, socklen_t );
static int (*real_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
static int (*real_system)(const char *);
int __libc_start_main(int *(main) (int, char * *, char * *), int argc, char * * argv, void (*init) (void), void (*fini) (void), void (*rtld_fini) (void), void (* stack_end)){
    // Load the real __libc_start_main
    void*handle=dlopen("libc.so.6",RTLD_LAZY);
    //int (*real___libc_start_main)(int (*main)(int, char**, char**), int argc, char **ubp_av, void (*init)(void), void (*fini)(void), void (*rtld_fini)(void), void (*stack_end));
    
    //real___libc_start_main = dlsym(RTLD_NEXT, "__libc_start_main");

    int (*real__libc_start_main)(int *(main) (int, char * *, char * *), int argc, char * * argv, void (*init) (void), void (*fini) (void), void (*rtld_fini) (void), void (* stack_end));
    real__libc_start_main=dlsym(handle,"__libc_start_main");
    dlclose(handle);
    // Load the real functions we need to hijack
    
    // Hijack the entry point and perform the necessary initializations
    //system("cat /proc/self/maps");
    //printf("-----------test_t\n");
    //long open_addr;

    
    init_t(argv);

    //dlopen(libc.so.6) ---->  dlsym("_libc_start_main")
    //先call 真正的open 存起來 再蓋掉got table的位置 則使用時會呼叫到我的open 進行監控後再決定是否呼叫真正的open
    //printf("intit ok\n");
    
    // Call the real __libc_start_main
    return (*real__libc_start_main)(main, argc, argv, init, fini, rtld_fini, stack_end);
}
int open_t(const char *pathname, int flags,mode_t mode){
    
    //printf("pass open\n");
    char* LOGGER_FD=getenv("LOGGER_FD");
    long log=strtol(LOGGER_FD,NULL,10);
    
    
    char*config_env=getenv("SANDBOX_CONFIG");
    FILE*config_fp=fopen(config_env,"r");
    //printf("%s\n",pathname);
    char keyword[1024];
    char*line_open=NULL;
    size_t len_open=0;
    
    if(realpath(pathname,keyword)==NULL){
        fprintf(stderr,"open_realpath_fail");
        exit(EXIT_FAILURE);
    }
    //printf("%s\n",keyword);
    while(getline(&line_open,&len_open,config_fp)!=-1){
        char *buf=strtok(line_open," ");
        
        
        if((strcmp(buf,"BEGIN")==0)){
            continue;
        }
        
        char temp[1024];
        strncpy(temp,buf,strlen(buf)-2);
        
        if(strcmp(keyword,temp)==0){
            //printf("set errno\n");
            errno=EACCES;
            //perror(errno);
            dprintf(log,"[logger] open (\"%s\",%d,%d) = -1 \n",pathname,flags,mode);
            return -1;
        }
        else if(strcmp(buf,"END")==0) break;
    }
    fclose(config_fp);
    int open_return=real_open(pathname,flags,mode);
    dprintf(log,"[logger] open (\"%s\",%d,%p) = %d\n",pathname,flags,mode,open_return);
    //return (*real_open)(pathname,flags,mode);
    return open_return;
}
ssize_t read_t(int fd,void*buf,size_t count){
    //printf("buf:%s\n",buf);
    //printf("my read:%s\n",buf);
    //off_t pos=lseek(fd,0,SEEK_CUR);
    //memset(buf,0,sizeof(buf));
    //printf("read ok\n");
    long read_return=real_read(fd,buf,count);
    //char temp[1024];
    //strncpy(temp,buf,27);
    char* LOGGER_FD=getenv("LOGGER_FD");
    long log=strtol(LOGGER_FD,NULL,10);
    
    char*config_env=getenv("SANDBOX_CONFIG");
    FILE*fp_read=fopen(config_env,"r");
    char *keyword[1024];
    char*line_read=NULL;
    size_t len_read=0;
    int flag=0;
    char block_arr[100][1000];
    while(getline(&line_read,&len_read,fp_read)!=-1){
        //printf("strlen:%d line : %s\n",strlen(line_get),line_get);
        char temp[10240]="";
        strncpy(temp,line_read,strlen(line_read)-2);
        //strncpy(temp,line_get,strlen(line_get)-2);
        //printf("strlen:%d (temp)\n",strlen(temp));
        if(strncmp(temp,"BEGIN read-blacklist",20)==0){
            flag++;
            //printf("match\n");
        }
        else if(strncmp(temp,"END read-blacklist",18)==0){
            break;
        }else if(flag){
            memset(block_arr[flag-1],0,strlen(temp)+1);
            memcpy(block_arr[flag-1],temp,strlen(temp));
            if(strstr(buf,block_arr[flag-1])!=NULL){
                errno=EIO;
                //printf("block\n");
                dprintf(log,"[logger] read (%d,%p,%ld) = -1\n",fd,&buf,count);
                return -1;
            }
            flag++;
        }
    }
    fclose(fp_read);
    pid_t pid=getpid();
    char filename[100];
    sprintf(filename,"%d-%d-read.log",pid,fd);
    int fp_create;
    if(access(filename,F_OK)!=0){
        //mode_t fileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        fp_create=open(filename,O_RDWR|O_CREAT);
        off_t offset=lseek(fp_create,0,SEEK_SET);
        //printf("no exit filename:%s,fp_create:%d",filename,fp_create);
        //printf("read no exit\n");
    }else{
        fp_create=open(filename,O_RDWR|O_CREAT);
        if(fp_create==-1){
            fprintf(stderr,"fail create file (read)\n");
            exit(EXIT_FAILURE);
        }
        
        //printf("exit filename:%s,fp_create:%d",filename,fp_create);
        for(int i=0;i<flag-1;i++){
            off_t offset=lseek(fp_create,-strlen(block_arr[i]),SEEK_END);
            
            char check_buf[10240]="";
            long check_len=real_read(fp_create,check_buf,10240);
            
            if(check_len<strlen(block_arr[i])) continue;
            //printf("create ok i:%d\n",i);
            char*buf_temp=buf;
            //strncpy(buf_temp,(char*)buf,strlen(block_arr));
            //printf("buf_copy ok\n");
            strcat(check_buf,buf_temp);
            if(strstr(check_buf,block_arr[i])!=NULL){
                errno=EIO;
                dprintf(log,"[logger] read (%d,%p,%ld) = -1\n",fd,&buf,count);
                return -1;
            }
            lseek(fp_create,strlen(block_arr[i]),SEEK_SET); 
        }
        //printf("read exit\n");
        
    }
    
    //int check_buf=real_read(fp_create)
    //off_t offset_w=lseek(fp_create,0,SEEK_END);
    real_write(fp_create,buf,strlen(buf));
    close(fp_create);
    
    

    dprintf(log,"[logger] read (%d,%p,%ld) = %ld\n",fd,&buf,count,read_return);
    return read_return;

    //return (*real_read)(fd,buf,count);
}
ssize_t write_t(int fd, const void *buf, size_t count){
    //printf("pass write\n");
    pid_t pid=getpid();
    char filename[100];
    int fp_create;
    sprintf(filename,"%d-%d-write.log",pid,fd);
    if(access(filename,F_OK)!=0){
        mode_t fileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        fp_create=open(filename,O_RDWR|O_CREAT,fileMode);
        off_t offset=lseek(fp_create,0,SEEK_SET);
    }else{
        fp_create=open(filename,O_RDWR);
        off_t offset=lseek(fp_create,0,SEEK_END);
    }
    if(fp_create==-1){
        fprintf(stderr,"fail create file (write)");
        exit(EXIT_FAILURE);
    }
    real_write(fp_create,buf,strlen(buf));
    close(fp_create);
    char* LOGGER_FD=getenv("LOGGER_FD");
    long log=strtol(LOGGER_FD,NULL,10);
    dprintf(log,"[logger] write (%d,%p,%ld) = %ld\n",fd,&buf,count,strlen(buf));
    return (*real_write)(fd,buf,count);
}
int connect_t(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    //printf("connect ok\n");
    char *ip_s=inet_ntoa(((struct sockaddr_in *)addr)->sin_addr);
    char ip[1000];
    strcpy(ip,ip_s);
    //printf("%s\n",ip);
    FILE*fp_con=fopen("./config.txt","r");
    char *keyword[1024];
    char*line_con=NULL;
    size_t len_con=0;
    int flag=0;

    char* LOGGER_FD=getenv("LOGGER_FD");
    long log=strtol(LOGGER_FD,NULL,10);

    while(getline(&line_con,&len_con,fp_con)!=-1){
        //printf("strlen:%d line : %s\n",strlen(line_con),line_con);
        char temp[1000]="";
        
        strncpy(temp,line_con,strlen(line_con)-2);
        
        //strncpy(temp,line_get,strlen(line_get)-2);
        //printf("strlen:%d (temp)\n",strlen(temp));
        if(strncmp(temp,"BEGIN connect-blacklist",23)==0&&strlen(temp)==23){
            flag++;
            //printf("temp:%s\n",temp);
        }
        else if(strncmp(line_con,"END connect-blacklist",21)==0){
            break;
        }else if(flag){
            //printf("ip:%s  temp:%s\n",ip,temp);
                char *tt=strtok(temp,":");
                //printf("tt:%s\n",tt);
                struct hostent *host;
                struct in_addr **addr_list;
                int i;

                host = gethostbyname(tt);
                if (host == NULL) {
                    printf("fail to gethost\n");
                    return 1;
                }

                addr_list = (struct in_addr **)host->h_addr_list;

                for (i = 0; addr_list[i] != NULL; i++) {
                    if(strncmp(ip,inet_ntoa(*addr_list[i]),strlen(ip))==0){
                        //printf("fail connect\n");
                        dprintf(log,"[logger] connect (%d,%s,%d) = -1\n",sockfd,ip,addrlen);
                        errno=ECONNREFUSED;
                        return -1;
                    }
                    //printf("ip_addr%s\n", inet_ntoa(*addr_list[i]));
                }
            //printf("ip_addr:%s",ip_addr);
            
            //flag++;
        }
            
        }
    
    fclose(fp_con);
    dprintf(log,"[logger] connect (%d,%s,%d) = 0\n",sockfd,ip,addrlen);
    //return 0;
    return (*real_connect)(sockfd, addr,  addrlen);
}
int getaddrinfo_t(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res){
    int ret_getaddr=real_getaddrinfo(node,service,hints,res);
    char*config_env=getenv("SANDBOX_CONFIG");
    FILE*fp_get=fopen(config_env,"r");
    char *keyword[1024];
    char*line_get=NULL;
    size_t len_get=0;
    int flag=0;

    char* LOGGER_FD=getenv("LOGGER_FD");
    long log=strtol(LOGGER_FD,NULL,10);

    while(getline(&line_get,&len_get,fp_get)!=-1){
        //printf("strlen:%d line : %s\n",strlen(line_get),line_get);
        char temp[1000]="";
        strncpy(temp,line_get,strlen(line_get)-2);
        //strncpy(temp,line_get,strlen(line_get)-2);
        //printf("strlen:%d (temp)\n",strlen(temp));
        if(strncmp(temp,"BEGIN getaddrinfo-blacklist",27)==0){
            flag++;
            //printf("match\n");
        }
        else if(strncmp(line_get,"END getaddrinfo-blacklist",23)==0){
            break;
        }else if(flag){
            if(strncmp(node,temp,strlen(temp))==0){
                //printf("fail getaddrinfo\n");
                dprintf(log,"[logger] getaddrinfo (%s,%s,%p,%p) = -2\n",node,service,hints,res);
                return EAI_NONAME;
            }
            flag++;
        }
    }
    fclose(fp_get);
    dprintf(log,"[logger] getaddrinfo (%s,%s,%p,%p) = %d\n",node,service,hints,res,ret_getaddr);
    return ret_getaddr;
}

int system_t(const char *command){
    char* LOGGER_FD=getenv("LOGGER_FD");
    long log=strtol(LOGGER_FD,NULL,10);
    dprintf(log,"[logger] system (%s)\n",command);
    return (*real_system)(command);
}

int init_t(char**argv) {
    FILE*fp_base=fopen("/proc/self/maps","r");
    if(fp_base==-1){
        printf("can not open\n");
        //return -1;
    }
    //printf("%s\n",argv[1]);
    
    //printf("ok\n");
    char*line=NULL;
    size_t len=0;
    unsigned long addr_array[5];
    int count=0;
    while(count<5&&getline(&line,&len,fp_base)!=-1){
        //printf("%s\n",line);
		char*buf=strtok(line,"-");
		long number=strtol(buf,NULL,16);
		addr_array[count++]=number;
        //printf("addr[%d]: %p\n",count-1,number);
        
	}
    fclose(fp_base);
    //printf("ok\n");
    if(mprotect(addr_array[3],addr_array[4]-addr_array[3],PROT_WRITE|PROT_READ)){
        perror("can not mprotect");
    }
    unsigned long base=addr_array[0];

    char filename[100]="";
    if(realpath("/proc/self/exe",filename)==NULL){
        fprintf(stderr,"Failed exist");
        exit(EXIT_FAILURE);
    }
    FILE*fp=fopen(filename,"rb");
    //printf("%s\n",filename);
    
    //char *filename = argv;
    //printf("%s\n",filename);
    //FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // 读取 ELF 文件头
    Elf64_Ehdr elf_header; //header
    if (fread(&elf_header, sizeof(Elf64_Ehdr), 1, fp) != 1) {
        fprintf(stderr, "Failed to read ELF header from file %s\n", filename);
        exit(EXIT_FAILURE);
    }
    //printf("elf\n");

    // 计算重定位表的地址和大小
    Elf64_Shdr *shdr_table = (Elf64_Shdr *)malloc(sizeof(Elf64_Shdr) * elf_header.e_shnum);
    if (!shdr_table) {
        fprintf(stderr, "Failed to allocate memory for section header table\n");
        exit(EXIT_FAILURE);
    }

    fseek(fp, elf_header.e_shoff, SEEK_SET);// section header start 
    if (fread(shdr_table, sizeof(Elf64_Shdr), elf_header.e_shnum, fp) != elf_header.e_shnum) {
        fprintf(stderr, "Failed to read section header table from file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    Elf64_Shdr *rela_plt_hdr = NULL;
    Elf64_Shdr *symtab_hdr = NULL;
    Elf64_Shdr *strtab_hdr = NULL;

    // 找到 .rela.plt 节
    int str_count=0;
    for (int i = 0; i < elf_header.e_shnum; i++) {
        // printf("header %d \n",shdr_table[i].sh_type);
        if (shdr_table[i].sh_type == SHT_RELA ) {
            //printf(".rela.plt\n");
            rela_plt_hdr = &shdr_table[i];
            // break;
        }
        else if (shdr_table[i].sh_type == SHT_DYNSYM) {
            //printf("dyn.sym\n");
            symtab_hdr = &shdr_table[i];
        }
        else if (shdr_table[i].sh_type == SHT_STRTAB && str_count==0) {
            //printf("strtab\n");
            strtab_hdr = &shdr_table[i];
            str_count=1;
        }
        // printf("%d\n",i);
    }

    if (!rela_plt_hdr) {
        fprintf(stderr, "Failed to find .rela.plt section in file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    if (!symtab_hdr) {
        fprintf(stderr, "Failed to find symbol table section in file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    if (!strtab_hdr) {
        fprintf(stderr, "Failed to find string table section in file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // 读取 .rela.plt 节中的内容
    fseek(fp, rela_plt_hdr->sh_offset, SEEK_SET);
    size_t num_relocations = rela_plt_hdr->sh_size / rela_plt_hdr->sh_entsize;
    Elf64_Rela *relocations = (Elf64_Rela *)malloc(sizeof(Elf64_Rela) * (num_relocations));
    if (!relocations) {
        fprintf(stderr, "Failed to allocate memory for relocations\n");
        exit(EXIT_FAILURE);
    }

    if (fread(relocations, rela_plt_hdr->sh_entsize, num_relocations, fp) != num_relocations) {
        fprintf(stderr, "Failed to read relocations from file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // 读取符号表和字符串表
    fseek(fp, symtab_hdr->sh_offset, SEEK_SET);
    size_t num_symbols = symtab_hdr->sh_size / symtab_hdr->sh_entsize;
    Elf64_Sym *symbols = (Elf64_Sym *)malloc(sizeof(Elf64_Sym) * num_symbols);
    if (!symbols) {
        fprintf(stderr, "Failed to allocate memory for symbols\n");
        exit(EXIT_FAILURE);
    }

    if (fread(symbols, symtab_hdr->sh_entsize, num_symbols, fp) != num_symbols) {
        fprintf(stderr, "Failed to read symbols from file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    fseek(fp, strtab_hdr->sh_offset, SEEK_SET);
    char *strtab = (char *)malloc(strtab_hdr->sh_size);
    if (!strtab) {
        fprintf(stderr, "Failed to allocate memory for string table\n");
        exit(EXIT_FAILURE);
    }

    if (fread(strtab, 1, strtab_hdr->sh_size, fp) != strtab_hdr->sh_size) {
        fprintf(stderr, "Failed to read string table from file %s\n", filename);
        exit(EXIT_FAILURE);
    }
    // for(int i=0;i<strtab_hdr->sh_size;i++){
    //     // if(!strncmp(&strtab[i],"open",4)){
    //         printf("%s %d\n",&strtab[i],i);

    // }
    // 在重定位表中查找 open 函数的符号
    unsigned long read_addr=0;
    unsigned long open_addr=0;
    unsigned long write_addr=0;
    unsigned long connect_addr=0;
    unsigned long getaddrinfo_addr=0;
    unsigned long system_addr=0;
    ssize_t (*my_read)(int , void *, size_t )=NULL;
    int (*my_open)(const char *,int,mode_t)=NULL;
    ssize_t (*my_write)(int , const void *, size_t )=NULL;
    int (*my_connect)(int , const struct sockaddr *, socklen_t )=NULL;
    int (*my_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **)=NULL;
    int (*my_system)(const char *)=NULL;
    for (int i = 0; i < num_relocations; i++) {
        Elf64_Rela *rela = &relocations[i];
        // printf("%d,%ld,%ld\n",i,ELF64_R_TYPE(rela->r_info),R_X86_64_JUMP_SLOT);
        if (ELF64_R_TYPE(rela->r_info) == R_X86_64_JUMP_SLOT) {
            Elf64_Sym *sym = &symbols[ELF64_R_SYM(rela->r_info)];
            char *symname = &strtab[sym->st_name];
            
            //printf("%s %lx st_name:%d\n",symname,rela->r_offset,sym->st_name);
            // printf("o")
            if (strncmp(symname, "read",4) == 0 && strlen(symname)==4) {
                read_addr=rela->r_offset;
                unsigned long *real_read_addr=(unsigned long*)(base+read_addr);
                real_read=*real_read_addr;
                my_read=read_t;
                *real_read_addr=my_read;

                //printf("Found read at index %d, offset 0x%lx\n", i, rela->r_offset);
            }else if(strncmp(symname,"open",4)==0 && strlen(symname)==4){
                open_addr=rela->r_offset;
                unsigned long *real_open_addr=(unsigned long*)(base+open_addr);
                //printf("real_open:%p\n",real_open_addr);
                real_open=*real_open_addr;
                my_open=open_t;
                *real_open_addr=my_open;
                //printf("Found open at index %d, offset 0x%lx\n", i, rela->r_offset);
            }else if(strncmp(symname,"write",5)==0 && strlen(symname)==5){
                write_addr=rela->r_offset;
                unsigned long*real_write_addr=(unsigned long*)(base+write_addr);
                real_write=*real_write_addr;
                my_write=write_t;
                *real_write_addr=my_write;
                //printf("Found write at index %d, offset 0x%lx\n", i, rela->r_offset);
            }else if(strncmp(symname,"connect",7)==0 && strlen(symname)==7){
                connect_addr=rela->r_offset;
                unsigned long*real_connect_addr=(unsigned long*)(base+connect_addr);
                real_connect=*real_connect_addr;
                my_connect=connect_t;
                *real_connect_addr=my_connect;
                //printf("Found connect at index %d, offset 0x%lx\n", i, rela->r_offset);
            }else if(strncmp(symname,"getaddrinfo",11)==0 && strlen(symname)==11){
                getaddrinfo_addr=rela->r_offset;
                unsigned long*real_getaddrinfo_addr=(unsigned long*)(base+getaddrinfo_addr);
                real_getaddrinfo=*real_getaddrinfo_addr;
                my_getaddrinfo=getaddrinfo_t;
                *real_getaddrinfo_addr=my_getaddrinfo;
                //printf("Found getaddrinfo at index %d, offset 0x%lx\n", i, rela->r_offset);
            }else if(strncmp(symname,"system",6)==0 && strlen(symname)==6){
                system_addr=rela->r_offset;
                unsigned long*real_system_addr=(unsigned long*)(base+system_addr);
                real_system=*real_system_addr;
                my_system=system_t;
                *real_system_addr=my_system;
            }
        }
    }

    fclose(fp);
    free(relocations);
    free(symbols);
    free(strtab);
    //system("cat /proc/self/maps");
    //printf("--------------hw1_gpt\n");
    
    
    

    

    //void*handle=dlopen("libtest.so",RTLD_LAZY);
    //unsigned long* my_open=NULL;
    //if(handle!=NULL) my_open=dlsym(handle,"open_t");
    //*real_open_addr=func_open_t;
    //printf("open_t ok\n");
    //dlclose(handle);
    
    

    //open();
    

    return 0;
}

