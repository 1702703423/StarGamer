/*
*   @file  dl_lib.h
*   @author wzh
*   @version 1.0
*   @date 2021-12-3
*   @description  动态加载实现源码
*/
#include "dl_lib.h"
#include "dl_elf.h"
#include "dl_port.h"

//加载elf文件到句柄中
//pHandle：句柄
//lib_ptr：文件缓存首地址
//返回值：DL_Err_Type 解析过程中遇到的错误
DL_Err_Type dl_load_lib(DL_Handler* pHandle,const void* const lib_ptr){
	bool has_vstart=false;
	Elf_Addr vstart_addr=0, vend_addr=0;
	uint32_t dynsym_off;
	uint32_t max_align;
	DL_Err_Type err=DL_NO_ERR;
	
	
	if(memcmp(elf_header(lib_ptr)->e_ident,ELFMAG,SELFMAG)!=0){
		err=DL_FILE_INVALID;
		goto fail0;
	}

	if(elf_header(lib_ptr)->e_ident[EI_CLASS]!=ELFCLASS32){
		err=DL_PLATFORM_ERR;
		goto fail0;
	}
	
	if(elf_header(lib_ptr)->e_type!=ET_DYN&&elf_header(lib_ptr)->e_type!=ET_EXEC){
		err=DL_ELF_NOT_SUPPRORT;
		goto fail0;
	}
	
	
	//计算加载区的首地址以及所有加载区的大小
	for(size_t i = 0;i<elf_header(lib_ptr)->e_phnum;i++){
		if(elf_phdr(lib_ptr)[i].p_type!=PT_LOAD)
			continue;
		DL_LOG_D("LOAD segment: %d, 0x%p, 0x%08x\r\n", i, (void*)elf_phdr(lib_ptr)[i].p_vaddr, elf_phdr(lib_ptr)[i].p_memsz);
		if (elf_phdr(lib_ptr)[i].p_memsz < elf_phdr(lib_ptr)[i].p_filesz){
            DL_LOG_E("invalid elf: segment %d: p_memsz: %d, p_filesz: %d",\
				i, elf_phdr(lib_ptr)[i].p_memsz, elf_phdr(lib_ptr)[i].p_filesz);
			err=DL_FILE_INVALID;
            goto fail0;
        }
		if(has_vstart==false){
			vstart_addr = elf_phdr(lib_ptr)[i].p_vaddr;
            vend_addr = elf_phdr(lib_ptr)[i].p_vaddr + elf_phdr(lib_ptr)[i].p_memsz;
            has_vstart = true;
			max_align = elf_phdr(lib_ptr)[i].p_align;
            if (vend_addr < vstart_addr){
                DL_LOG_E("invalid elf: segment %d: p_vaddr: %d, p_memsz: %d",
                           i, elf_phdr(lib_ptr)[i].p_vaddr, elf_phdr(lib_ptr)[i].p_memsz);
                err=DL_FILE_INVALID;
				goto fail0;
            }
		}
		else{
			if (elf_phdr(lib_ptr)[i].p_vaddr < vend_addr){
                DL_LOG_E("invalid elf: segment should be sorted and not overlapped");
                err=DL_FILE_INVALID;
				goto fail0;
            }

            vend_addr = elf_phdr(lib_ptr)[i].p_vaddr + elf_phdr(lib_ptr)[i].p_memsz;
            if (vend_addr < elf_phdr(lib_ptr)[i].p_vaddr){
                DL_LOG_E("invalid elf: "
                           "segment %d address overflow", i);
                err=DL_FILE_INVALID;
				goto fail0;
            }
			if(elf_phdr(lib_ptr)[i].p_align>max_align){
				max_align=elf_phdr(lib_ptr)[i].p_align;
			}
		}
	}
	
	//根据加载区大小分配内存
	pHandle->mem_size=vend_addr-vstart_addr;
	pHandle->vstart_addr=vstart_addr;
	DL_LOG_D("module size: %d, vstart_addr: 0x%p", pHandle->mem_size, (void*)vstart_addr);
	if(pHandle->mem_size==0){
		DL_LOG_E("Module: size error\n");
		err=DL_MEMSIZE_ERR;
        goto fail0;
	}
	pHandle->mem_ptr=dl_port_aligned_alloc(max_align,pHandle->mem_size);
	if(pHandle->mem_ptr==NULL){
		DL_LOG_E("Module: allocate space failed.");
		err=DL_MALLOC_FAIL;
        goto fail0;
	}
	memset(pHandle->mem_ptr,0,pHandle->mem_size);
	
	//复制文件数据到加载区上
	for(size_t i=0;i<elf_header(lib_ptr)->e_phnum;i++){
		if(elf_phdr(lib_ptr)[i].p_type==PT_LOAD){
			memcpy((uint8_t*)pHandle->mem_ptr+elf_phdr(lib_ptr)[i].p_vaddr-vstart_addr,\
					(uint8_t*)lib_ptr+elf_phdr(lib_ptr)[i].p_offset,\
					elf_phdr(lib_ptr)[i].p_filesz);
		}
	}
	
	//此处区分共享库与可执行文件
	if(elf_header(lib_ptr)->e_entry!=0){//若存在程序入口点
		pHandle->entry_addr=(uint8_t*)pHandle->mem_ptr + elf_header(lib_ptr)->e_entry - vstart_addr;
	}
	else{
		pHandle->entry_addr=NULL;
	}
	
	//开始重定向
	//寻找带有重定向信息的节区
	for(size_t i=0;i<elf_header(lib_ptr)->e_shnum;i++){
		Elf_Sym *symtab;
        Elf_Rel *rel;
        char *strtab;
		size_t nr_reloc;
        if (!IS_REL(elf_shdr(lib_ptr)[i]))
            continue;
      
		//获取重定向表项的表头
		rel = (Elf_Rel *)((uint8_t *)lib_ptr + elf_shdr(lib_ptr)[i].sh_offset);
		//获取动态符号表的表头
		symtab = (Elf_Sym *)((uint8_t *)lib_ptr + elf_shdr(lib_ptr)[elf_shdr(lib_ptr)[i].sh_link].sh_offset);
		//获取动态符号代表的字符串表的表头
        strtab = (char*)((uint8_t *)lib_ptr +elf_shdr(lib_ptr)[elf_shdr(lib_ptr)[elf_shdr(lib_ptr)[i].sh_link].sh_link].sh_offset);
        nr_reloc = (size_t)(elf_shdr(lib_ptr)[i].sh_size / sizeof(Elf_Rel));
		for(size_t j=0;j<nr_reloc;j++){
			extern DL_Err_Type dl_relocate(DL_Handler *pHandle, Elf32_Rel *rel, Elf32_Addr sym_val);
            Elf_Sym *sym = &symtab[ELF32_R_SYM(rel[j].r_info)];
			
			if ((sym->st_shndx != SHT_NULL) ||(ELF_ST_BIND(sym->st_info) == STB_LOCAL)){//如果符号可以解析
                Elf_Addr addr;

                addr = (Elf_Addr)((uint8_t*)pHandle->mem_ptr + sym->st_value - vstart_addr);
                err=dl_relocate(pHandle, &rel[j], addr);
				if(err!=DL_NO_ERR){
					goto fail1;
				}
            }
            else{//从外部导入符号
				 //由于编译器原因暂不支持
				DL_LOG_E("Module: can't find %s in kernel symbol table", strtab + sym->st_name);
				err=DL_UNDEF_SYM_ERR;
				goto fail1;
            }
		}
	}
	
	//构建符号表
	for(dynsym_off=0;dynsym_off<elf_header(lib_ptr)->e_shnum;dynsym_off++){
		uint8_t *shstrab;
		shstrab = (uint8_t *)lib_ptr +
                  elf_shdr(lib_ptr)[elf_header(lib_ptr)->e_shstrndx].sh_offset;
		if(strcmp((const char *)(shstrab + elf_shdr(lib_ptr)[dynsym_off].sh_name), ELF_DYNSYM)==0)
			break;
	}
	
	if(dynsym_off!=elf_header(lib_ptr)->e_shnum){
		int count = 0;
        Elf_Sym  *symtab = NULL;
        uint8_t *strtab = NULL;

        symtab = (Elf_Sym *)((uint8_t *)lib_ptr + elf_shdr(lib_ptr)[dynsym_off].sh_offset);
        strtab = (uint8_t *)lib_ptr + elf_shdr(lib_ptr)[elf_shdr(lib_ptr)[dynsym_off].sh_link].sh_offset;

		//查找动态段符号个数，方便预分配内存
        for (size_t i = 0; i < elf_shdr(lib_ptr)[dynsym_off].sh_size / sizeof(Elf_Sym); i++){
            if ((ELF_ST_BIND(symtab[i].st_info) == STB_GLOBAL) &&
                (ELF_ST_TYPE(symtab[i].st_info) == STT_FUNC))
                count ++;
        }
		
		pHandle->nsym = count;
		pHandle->symtab = (DL_Symbol_Type *)dl_port_malloc(count*sizeof(DL_Symbol_Type));
		if(pHandle->nsym != 0){
			if(pHandle->symtab == NULL){
				DL_LOG_E("Module: allocate space failed.");
				err=DL_MALLOC_FAIL;
				goto fail2;
			}
			memset(pHandle->symtab,0,count*sizeof(DL_Symbol_Type));
			
			//将动态符号段的内容复制到内存中
			for (size_t i = 0, j = 0; i < elf_shdr(lib_ptr)[dynsym_off].sh_size / sizeof(Elf_Sym); i++){
				size_t length;
				//仅保留全局函数类型的符号
				if ((ELF_ST_BIND(symtab[i].st_info) != STB_GLOBAL) ||
					(ELF_ST_TYPE(symtab[i].st_info) != STT_FUNC))
					continue;

				length = strlen((const char *)(strtab + symtab[i].st_name)) + 1;

				pHandle->symtab[j].addr =
					(void *)((uint8_t*)pHandle->mem_ptr + symtab[i].st_value - pHandle->vstart_addr);
				pHandle->symtab[j].name = dl_port_malloc(length);
				if(pHandle->symtab[j].name==NULL){
					DL_LOG_E("Module: allocate space failed.");
					err=DL_MALLOC_FAIL;
					goto fail3;
				}
				memcpy((void *)pHandle->symtab[j].name,
						  strtab + symtab[i].st_name,
						  length);
				((char*)pHandle->symtab[j].name)[length-1]='\0';
				j ++;
			}
		}
	}
	else{
		pHandle->symtab=NULL;
		pHandle->nsym=0;
	}
	#if defined(DL_CACHE_USE)&& DL_CACHE_USE==1
	dl_port_cache_process(pHandle->mem_ptr,pHandle->mem_size);
	#endif
	return err;
fail3:	
	for(size_t i = 0;i < pHandle->nsym;i++){
		dl_port_free((void*)pHandle->symtab[i].name);
	}
fail2:
	dl_port_free(pHandle->symtab);
fail1:
	dl_port_aligned_free(pHandle->mem_ptr);
fail0:
	return err;
}

//获取库中的函数指针
//pHandle：句柄
//sys_name：函数名
//返回值：void* 函数指针（等于NULL时，表示未找到该函数）
void* dl_get_func(DL_Handler* pHandle,const char* sys_name){
	for(size_t i=0;i<pHandle->nsym;i++){
		if(strcmp(sys_name,pHandle->symtab[i].name)==0){
			return pHandle->symtab[i].addr;
		}
	}
	return NULL;
}

//获取库中的函数指针
//pHandle：句柄
//返回值：(int(*)(int, char *[])) 函数指针（等于NULL时，表示没有入口函数）
int(*dl_get_entry(DL_Handler* pHandle))(int argc, char *argv[]){
	return (int(*)(int, char *[]))pHandle->entry_addr;
}

//释放加载库的内存
//pHandle：句柄
void dl_destroy_lib(DL_Handler* pHandle){
	for(size_t i=0;i<pHandle->nsym;i++){
		dl_port_free((void*)pHandle->symtab[i].name);
	}
	dl_port_free(pHandle->symtab);
	dl_port_aligned_free(pHandle->mem_ptr);
}

#if defined(DL_USE_FILE_SYSTEM)&&DL_USE_FILE_SYSTEM==1
//加载库文件到句柄中
//pHandle：句柄
//file_name：文件名
//返回值：DL_Err_Type 错误码
DL_Err_Type dl_load_file(DL_Handler* pHandle,const char* file_name){
	DL_Err_Type err=DL_NO_ERR;
	void* pfile;
	uint8_t* file_buf;
	
	if((pfile=dl_port_open_file(file_name))==NULL){
		err=DL_FILE_OPEN_ERR;
		goto fail0;
	}

	file_buf=dl_port_malloc(dl_port_get_file_size(pfile));
	if(file_buf==NULL){
		err=DL_MALLOC_FAIL;
		goto fail1;
	}

	if(dl_port_read_file(pfile,file_buf,dl_port_get_file_size(pfile))==false){
		err=DL_FILE_OPEN_ERR;
		goto fail2;
	}
	
	err=dl_load_lib(pHandle,(void*)file_buf);
	if(err!=DL_NO_ERR){
		goto fail2;
	}
	
	dl_port_free(file_buf);
	dl_port_close_file(pfile);
	
	return err;
fail2:
	dl_port_free(file_buf);
fail1:	
	dl_port_close_file(pfile);
fail0:
	return err;
}

//直接运行一个文件
//file_name:文件名
//argc：参数数量
//argv：参数字符串数组
//ret：返回值
DL_Err_Type dl_exec_file(const char* file_name,int argc,char* argv[],int* ret){
	DL_Handler pHandle;
	DL_Err_Type err=DL_NO_ERR;
	*ret=-1;
	err=dl_load_file(&pHandle,file_name);
	if(err!=DL_NO_ERR)
		return err;
	if(dl_get_entry(&pHandle)!=NULL){
		*ret=dl_get_entry(&pHandle)(argc,argv);
		dl_destroy_lib(&pHandle);
		return DL_NO_ERR;
	}
	else{
		dl_destroy_lib(&pHandle);
		return DL_ELF_NO_ENTRY;
	}
}
#endif

