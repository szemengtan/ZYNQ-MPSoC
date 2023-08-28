int sys_init();
void sys_cleanup();

// Logging declarations
enum metal_log_level {
    METAL_LOG_EMERGENCY,	/**< system is unusable.               */
    METAL_LOG_ALERT,	/**< action must be taken immediately. */
    METAL_LOG_CRITICAL,	/**< critical conditions.              */
    METAL_LOG_ERROR,	/**< error conditions.                 */
    METAL_LOG_WARNING,	/**< warning conditions.               */
    METAL_LOG_NOTICE,	/**< normal but significant condition. */
    METAL_LOG_INFO,		/**< informational messages.           */
    METAL_LOG_DEBUG,	/**< debug-level messages.             */
};

typedef void (*metal_log_handler)(enum metal_log_level,
                const char *, ...);
void metal_set_log_handler(metal_log_handler);
metal_log_handler metal_get_log_handler(void);
void metal_set_log_level(enum metal_log_level);
enum metal_log_level metal_get_log_level(void);
void metal_default_log_handler(enum metal_log_level, const char *, ...);

/**********************************************************************
Routines added to support shmem_demo
***********************************************************************/
struct metal_device {
	const char             *name;       /**< Device name */
    ...;
};

struct metal_io_region{
	void			*virt;      /**< base virtual address */
    size_t          size;
    ...;
};

typedef enum {
	memory_order_relaxed,
	memory_order_consume,
	memory_order_acquire,
	memory_order_release,
	memory_order_acq_rel,
	memory_order_seq_cst,
} memory_order;

typedef unsigned long metal_phys_addr_t;

uint64_t metal_io_read32_explicit(struct metal_io_region *, unsigned long, memory_order);
uint64_t metal_io_read32(struct metal_io_region *, unsigned long);
void metal_io_write32_explicit(struct metal_io_region *, unsigned long, uint64_t, memory_order);
void metal_io_write32(struct metal_io_region *, unsigned long, uint64_t);

uint64_t metal_io_read64_explicit(struct metal_io_region *, unsigned long, memory_order);
uint64_t metal_io_read64(struct metal_io_region *, unsigned long);
void metal_io_write64_explicit(struct metal_io_region *, unsigned long, uint64_t, memory_order);
void metal_io_write64(struct metal_io_region *, unsigned long, uint64_t);

int metal_io_block_read(struct metal_io_region *, unsigned long, void *restrict, int);
int metal_io_block_set(struct metal_io_region *, unsigned long, unsigned char, int);
int metal_io_block_write(struct metal_io_region *, unsigned long, const void *restrict, int);
size_t metal_io_region_size(struct metal_io_region *);

metal_phys_addr_t metal_io_phys(struct metal_io_region *, unsigned long);
unsigned long metal_io_phys_to_offset(struct metal_io_region *, metal_phys_addr_t);

void *metal_io_virt(struct metal_io_region *, unsigned long);

void *metal_allocate_memory(unsigned int);
void metal_free_memory(void *);

int metal_device_open(const char *, const char *, struct metal_device **);
struct metal_io_region *metal_device_io_region(struct metal_device *, unsigned int index);
void metal_device_close(struct metal_device *);

int memcmp(void *, void *, unsigned int);
void dump_buffer(void *, unsigned int);
void print_demo(char *);

/**********************************************************************
Routines added to support shmem_atomic_demo
***********************************************************************/
int atomic_shmem_demo();

/**********************************************************************
Routines added to support ipi_shmem_demo
***********************************************************************/
typedef struct {...;} atomic_flag;
typedef struct {...;} atomic_int;

void set_remote_nkicked_ptr(atomic_flag *ptr);
void ipi_kick_register_handler_shmem_demo(void);

void atomic_flag_clear(atomic_flag *);
bool atomic_flag_test_and_set(atomic_flag *);

int init_ipi(void);
void enable_ipi_kick(void);
void disable_ipi_kick(void);
void deinit_ipi(void);
void kick_ipi(void *msg);
void wait_for_notified(atomic_flag *);