#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nurican Ozcan- Murat Tosun");
MODULE_DESCRIPTION("psvis module: a module that returns the process tree starting from a given PID");

static int pid = 1; //default pid for init process

/*
 * module_param(foo, int, 0000)
 * The first param is the parameters name
 * The second param is its data type
 * The final argument is the permissions bits,
 * for exposing parameters in sysfs (if non-zero) at a later stage.
 */

int simple_init(void);
void simple_exit(void);

module_param(pid, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(pid, "starting process' PID for process tree");

//module_param(age, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
//MODULE_PARM_DESC(age, "age of the caller");

static struct proc_dir_entry *proc_file;

//we are going to use DFS to search the tree.
static void dfs_process_tree(struct task_struct *task, struct seq_file *file, int depth){
	struct task_struct *child;
	struct list_head *list;

	//we need to put indentation for the deeper nodes
	int i; // to fix compilation problem
	for(i=0;i<depth;i++){
		seq_printf(file,"  ");
	}
	seq_printf(file, "-> PID: %d, Command: %s (Depth:%d)\n",task->pid, task->comm,depth);

	//then we define our recursive call the check the children
	list_for_each(list, &task->children){
		child = list_entry(list,struct task_struct, sibling);
		dfs_process_tree(child,file,depth+1); //we use recursive calls for each children
	}
}

//reading /proc/psvis
static int proc_show(struct seq_file *file, void *v){
	struct pid *pid_struct = find_get_pid(pid);
	struct task_struct *task = pid_task(pid_struct, PIDTYPE_PID);

	if(!task){
		seq_printf(file,"PID %d not found.\n",pid);
		return 0;
	}

	seq_printf(file,"Process Tree Starting From PID: %d\n",pid);
	dfs_process_tree(task,file,0); //start searching with DFS this is the first call for DFS
	return 0;
}

//to handle file opening with proc
static int proc_open(struct inode *inode, struct file *file){
	return single_open(file,proc_show,NULL);
}

//file operations for /proc/psvis
//
static const struct proc_ops proc_fops ={
	.proc_open = proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,};


// A function that runs when the module is first loaded
int simple_init(void) {
	proc_file = proc_create("psvis",0, NULL, &proc_fops);
	if(!proc_file){
		printk(KERN_ERR "Failed to create /proc/psvis\n");
		return 0;
	}
	printk(KERN_INFO "psvis module loaded. Starting PID: %d\n",pid);
	return 0;
}

// A function that runs when the module is removed
void simple_exit(void){
	proc_remove(proc_file);
	printk(KERN_INFO "psvis module unloaded.\n");
}

module_init(simple_init);
module_exit(simple_exit);
