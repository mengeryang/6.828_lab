// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace","Display function call frames", mon_backtrace},
	{ "showmapping","Display mappings between virtual address and physical address",mon_showmapping},
	{ "si","Singe step",mon_si},
	{ "c","Continue",mon_continue},
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	unsigned int* ebp = (unsigned int*)read_ebp();

	while(ebp != 0){
		handleEbp(ebp);
		ebp = (unsigned int *)*ebp;
	}
	return 0;
}

int 
mon_showmapping(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t start = ROUNDDOWN((uintptr_t)atoi(argv[1]),PGSIZE);
	uintptr_t end = ROUNDDOWN((uintptr_t)atoi(argv[2]),PGSIZE);
	pte_t *pte;
	uint32_t pa;
 	const char *tbl[5] = {"NOT PRESENT","RW|RW","R-|R-","RW|--","R-|--"};
	const char *perm;

	if(end < start){
		cprintf("invalid range\n");
		return -1;
	}

	for(uintptr_t i = start;i <= end;i = i + PGSIZE)
	{
		pte = pgdir_walk(kern_pgdir,(void*)i,0);
		if(pte == NULL)
			continue;
		pa = PTE_ADDR(*pte);
		switch(*pte & 0x7){
			case 1:
				perm = tbl[4];
				break;
			case 3:
				perm = tbl[3];
				break;
			case 5:
				perm = tbl[2];
				break;
			case 7:	
				perm = tbl[1];
				break;
			default:
				perm = tbl[0];
				break;
		}
		cprintf("%08x ----> %08x  %s\n",i,pa,perm);
	}
	
	
	return 0;
}

int 
mon_si(int argc, char **argv, struct Trapframe *tf)
{
	if(tf == NULL)
	{
		cprintf("no running process.\n");
		return 0;
	}
	
	tf->tf_eflags |= FL_TF;
	return -1;
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if(tf == NULL)
	{
		cprintf("no running process.\n");
		return 0;
	}
	
	tf->tf_eflags &= ~FL_TF;
	return -1;
}
/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

/**********customize tool function*********/
void handleEbp(unsigned int* ebp){

	int eip = *(ebp + 1);
	struct Eipdebuginfo info;
	int arg_num;
	char buff[80];
	debuginfo_eip((uintptr_t)eip, &info);
	arg_num = info.eip_fn_narg;

	cprintf("ebp %08x  eip %08x  args ",ebp,*(ebp+1));
	for(int i = 2;i < 7;i++){
		cprintf("%08x ",*(ebp + i));
	}
	cprintf("\n");
	strncpy(buff,(const char*)info.eip_fn_name,info.eip_fn_namelen);
	buff[info.eip_fn_namelen] ='\0';
	cprintf("\t%s:%d: %s+%d\n",info.eip_file,info.eip_line,buff,eip - info.eip_fn_addr);
}

uint32_t atoi(char* num){
	uint32_t result = 0;
	uint32_t base = 10;
	char char2num[128];
	
	memset(char2num,0x7f,128);
	char2num['0'] = 0;
	char2num['1'] = 1;
	char2num['2'] = 2;
	char2num['3'] = 3;
	char2num['4'] = 4;
	char2num['5'] = 5;
	char2num['6'] = 6;
	char2num['7'] = 7;
	char2num['8'] = 8;
	char2num['9'] = 9;
	char2num['a'] = 10;
	char2num['b'] = 11;
	char2num['c'] = 12;
	char2num['d'] = 13;
	char2num['e'] = 14;
	char2num['f'] = 15;
	
	
	while(*num == '0')
		num++;
	//if((*num < '0' || *num > '9') && (*num != 'x'))
		//return 0;
	if(*num == 'x'){
		base = 16;		
		num++;
	}
	while(char2num[(int)*num] < base){
		result = result*base + char2num[(int)*num];
		num++;
	}
	return result;
}


