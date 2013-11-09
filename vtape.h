#define	MTIOC			('m'<<8)
#define	MTIOCTOP		(MTIOC|1)	/* do a mag tape op */
#define	MTIOCGET		(MTIOC|2)	/* get tape status */
#define	MTIOCGETDRIVETYPE	(MTIOC|3)	/* get tape config data */
#define	MTIOCLTOP		(MTIOC|19)	/* do a mag tape op */
#define	MTWEOF			0		/* write an end-of-file record */
#define	MTFSF			1		/* forward space over file mark */
#define	MTBSF			2		/* backward space over file mark (1/2" only ) */
#define	MTFSR			3		/* forward space to inter-record gap */
#define	MTBSR			4		/* backward space to inter-record gap */
#define	MTREW			5		/* rewind */
#define	MTOFFL			6		/* rewind and put the drive offline */
#define	MTERASE			9		/* erase the entire tape */
#define	MTEOM			10		/* position to end of media */
//#define	MTNBSF			1		/* backward space file to BOF */
#define	MTSRSZ			12		/* set record size */
#define	MTGRSZ			13		/* get record size */
#define	MTLOAD			14		/* for loading a tape (use o_delay to open
				 		* the tape device) */
#define	MTBSSF			15		/* Backward space to x sequential filemarks */
#define	MTFSSF			16		/* Forward space to x sequential filemarks */
#define	MTTELL			17		/* get current logical block position */
#define	MTSEEK			18		/* position to logical block position */
	
#define WRITE_HEADER 		20
#define VALIDATE 		21
#define REWIND 			22
#define NON_REWIND 		23
#define SHOW_BACKUPS 		24
#define BLK_SIZE 		512
#define PADDING_CHAR 		0x1F
#define MAGICNO_OFFSET 		0
#define LABEL_OFFSET 		10
#define DESCR_OFFSET 		31
#define CREAT_DATE_OFFSET 	132
#define LASTMOD_DATE_OFFSET 	153
#define SIZE_OFFSET 		174
#define READ_HEAD_OFFSET 	180
#define WRITE_HEAD_OFFSET 	190
#define EOR 			0x1F
#define MAX_NO_OF_FILES 	100
#define vtape_OPENED      	0x1		/* lh is valid */
#define vtape_IDENTED     	0x2		/* li is valid */
#define	MAX_REC_SIZE		262144
#define MIN_REC_SIZE 		512

typedef struct file_table {
	char            filename[20];
	int             offset;
	int             no_of_rec;
	int             eof_flag;
}               file_table_struct;

typedef struct vtape_state {
	ldi_handle_t    lh;
	ldi_ident_t     li;
	dev_info_t     *dip;
	minor_t         minor;
	int             flags;
	kmutex_t        lock;
	int             write_oft;
	int             read_oft;
	int             ioctl_flag;
	int             rec_size;
	file_table_struct files[MAX_NO_OF_FILES];
	int             ERASE_FLAG;
	int             WRITE_HEADER_FLAG;	/* By default it 's value is
						 * 0. In case of write_header
						 * operation this flag is
						 * set, after writing the
						 * header this flag is reset
						 * in the vtape_write function. */
	int             VALIDATE_FLAG;		/* By default it 's value is 0. In
					 	* case of validate operation this
					 	* flag is set, after reading the
						 * header this flag is reset in the
					 	* vtape_read function. */
	int             REWIND_FLAG;		/* By default it 's value is 0
						 * stating that the tape is in REWIND
						 * mode. In case user wishes
						 * NON-REWIND mode, this flag is set. */
	int             total_no_files;		/* Total number of files currently on
						 * media */
	off_t           max_oft;		/* EOM offset */

}               vtape_state_t;

