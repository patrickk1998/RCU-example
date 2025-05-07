/* 
 * simple_fs.c - Demonstrating a simple read-only files system.
 */ 
#include <linux/init.h> /* Needed for the macros */ 
#include <linux/module.h> /* Needed by all modules */ 
#include <linux/printk.h> /* Needed for pr_info() */ 
#include <linux/sched.h>  /* Needed for current */
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/fs_types.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/version.h> 
#include <linux/pagemap.h> /* unlock_page */

typedef struct{
	char * name;
	unsigned long ino;	
	int type;	
	char * content;
} Entry;

Entry Entries[] = {
	{".", 1, DT_DIR, ""},
	{"..", 1, DT_DIR, ""},
	{"message.txt", 2, DT_REG, "Hello World!"}
};

static void simplefs_kill_sb(struct super_block *);
static struct inode *simplefs_iget(struct super_block *, unsigned long ino);
static int simplefs_fill_super(struct super_block *, void *, int); // <- there is a simple_fill_super function in linux/fs.h
static struct dentry *simple_mount(struct file_system_type *, int, const char *, void *data);

static struct file_system_type simple_fs_type = {
		.name           = "simplefs",
		.owner          = THIS_MODULE,
		.fs_flags       = FS_USERNS_MOUNT,
		.mount          = simple_mount,
		.kill_sb        = simplefs_kill_sb
};

static int ret = 0;

/*
 * What do we need?
 * 
 * void simplefs_kill_sb(struct super_block *) <- not found in rust because of 
 * struct inode *simplefs_iget(struct super_block *, unsigned long ino) 
 * int simplefs_fill_super(struct super_block *, void *, int) <- used by simplefs_mount
 * struct dentry *simplefs_mount(struct file_system_type *, int, const char *, void *data)
 *
 * DIR_FOPS
 *  > iterate_shared(struct file *, struct dir_context *)
 * DIR_IOPS 
 *  > Implement lookup(struct inode *, struct dentry *) <- without this calling open on the mount point will return
 *                                                         not a directory.
 *
 * REG_FOPS <- use generic_ro_fops
 * REG_AOPS <- used by generic_ro_fops
 * 	> Implement get_folio(struct file *, struct folio *)
 *
 */

static int simplefs_readdir(struct file *, struct dir_context *);
static struct dentry *simplefs_lookup(struct inode *, struct dentry *, unsigned int);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) 
static int simplefs_read_folio(struct file *, struct folio *);
#else 
static int simplefs_readpage(struct file *, struct page *);
#endif

static struct file_operations dir_fops = {
	.iterate_shared = simplefs_readdir,
};

static struct inode_operations dir_iops = {
	.lookup = simplefs_lookup,
};

static struct address_space_operations reg_aops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) 
	.read_folio = simplefs_read_folio,
#else 
	.readpage = simplefs_readpage,
#endif
};

static struct inode *simplefs_iget(struct super_block *sb, unsigned long ino){
	
	struct inode *inode;
	Entry *e;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	
	
	/*
		Set the new inode here.
	*/
	if(ino == 1)
		e = &Entries[0];	
	if(ino == 2)	
		e = &Entries[2];
	/*
	 * Without this, the root inode of the root directory 
	 * would not be of type directory. This will cause the mount()
	 * system call to return a ENOTDIR.
	 */
	if(e->type == DT_DIR){
		inode->i_mode = S_IFDIR | 0555; // <- directories need execute permission
		inode->i_fop = &dir_fops;
		inode->i_op = &dir_iops;
		inode->i_size = 512;
		inode->i_ino = ino;
	}
	

	if(e->type == DT_REG){
		inode->i_mode = S_IFREG | 0444;
		inode->i_fop = &generic_ro_fops;
		inode->i_size = strlen(e->content);
		inode->i_blocks = 1;
		inode->i_ino = ino;
		inode->i_private = e->content;
		inode->i_mapping->a_ops = &reg_aops;
	}
		
	return inode;
	
}

/*
 * There is a no memory associated with the superblock structure, other than the 
 * superblock itself.
 */
static void simplefs_kill_sb(struct super_block *sb)
{
    pr_info("kill_sb called\n"); 
	return;
}

static int simplefs_fill_super(struct super_block *sb, void *data, int silence)
{

	struct inode *inode;
	inode = simplefs_iget(sb, 1);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
			return -ENOMEM;
	return 0;	
}

static struct dentry *simple_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, simplefs_fill_super);
}


struct dentry *simplefs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags){
	
	struct inode *inode;
	int i;
	inode = NULL;
	pr_info("Called lookup %s\n", dentry->d_name.name);	
	for(i = 0; i < 3; i++){
		if(!strcmp(Entries[i].name, dentry->d_name.name)){
			inode = simplefs_iget(dir->i_sb, Entries[i].ino);	
		}
	}
	return d_splice_alias(inode, dentry);
}

/*
 * Must return number of directories read (greater than 0), and then zero, 
 * or else will be called in a continous loop.  
 */
static int simplefs_readdir(struct file *file, struct dir_context *cxt)
{
	int i;
	int amount = 0;
	pr_info("Read a directory\n");
	for(i = cxt->pos; i < 3; i++){
		amount++;
		cxt->pos++;
		if(!dir_emit(cxt, Entries[i].name, strlen(Entries[i].name), Entries[i].ino, Entries[i].type))
			return amount;
	}
	
	return amount;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) 

static int simplefs_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode;
	inode = folio->mapping->host; // have to use this since file may be NULL
	struct page *page;
	pr_info("Reading a Folio");

	if(folio->index > inode->i_size){
		goto out;
	} else {
		page = folio->page;
		memcpy(page_address(page), inode->i_private, inode->i_size);
		//memset(page_address(page) + inode->i_size, 0, PAGE_SZ - inode-i_size);	
		goto out;
	}

// Without this the task will be state: SLEEP_UNINTERRUPTABLE (waiting on IO)
out: 
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
}

#else 
static int simplefs_readpage(struct file *file, struct page *page)
{
	struct inode *inode;
	inode = page->mapping->host; // have to use this since file may be NULL
	pr_info("Reading a Page");
	if(page->index > inode->i_size){
		goto out;
	} else {
		memcpy(page_address(page), inode->i_private, inode->i_size);
		//memset(page_address(page) + inode->i_size, 0, PAGE_SZ - inode-i_size);	
		goto out;
	}

// Without this the task will be state: SLEEP_UNINTERRUPTABLE (waiting on IO)
out:
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
}
#endif

static int __init simplefs_init(void) 
{ 
	ret = register_filesystem(&simple_fs_type);

	if(!ret)
    	pr_info("filesystem module loaded\n"); 
	else 
		pr_info("filesystem module failed\n");

    return 0; 
} 
 
static void __exit simplefs_exit(void) 
{ 
	if(!ret)
		unregister_filesystem(&simple_fs_type);
		
    pr_info("filesystem module unloaded\n");

} 
 
module_init(simplefs_init); 
module_exit(simplefs_exit); 
 
MODULE_LICENSE("GPL");
