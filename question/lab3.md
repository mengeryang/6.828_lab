Ex9.
	
	Q: Why backtrace cause a kernel page fault?
	A: By printing the trapframe of this page fault we can find that the page fault happens at va 0xeebfe000. From memlayout.h we know that 0xeebfe000 is the beginning of area "Empty Memory" which is not mapped.
