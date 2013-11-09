#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/cmn_err.h>
#include <sys/mtio.h>
#include <sys/uio.h>
#include "vtape.h"

static int      vtape_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int      vtape_attach(dev_info_t *, ddi_attach_cmd_t);
static int      vtape_detach(dev_info_t *, ddi_detach_cmd_t);
static int      vtape_open(dev_t *, int, int, cred_t *);
static int      vtape_close(dev_t, int, int, cred_t *);
static int      vtape_write(dev_t, struct uio *, cred_t *);
static int      vtape_read(dev_t, struct uio *, cred_t *);
static int      vtape_ioctl(dev_t devt, int cmd, intptr_t arg, int mode, cred_t * credp, int *rval_p);
static void    *vtape_statep;
static offset_t oft;
static int      a[100], j, read_oft = 0, fileno, readfileno = 0;
static int      readflag = 0, closeflag, fileflag = 1;

static struct cb_ops vtape_cb_ops = {
	vtape_open,		/* open */
	vtape_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	vtape_read,		/* read */
	vtape_write,		/* write */
	vtape_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	NULL,			/* streamtab */
	D_NEW | D_MP,		/* cb_flag */
	CB_REV,			/* cb_rev */
	nodev,			/* aread */
	nodev			/* awrite */
};

static struct dev_ops vtape_dev_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	vtape_info,		/* getinfo */
	nulldev,		/* identify */
	nulldev,		/* probe */
	vtape_attach,		/* attach */
	vtape_detach,		/* detach */
	nodev,			/* reset */
	&vtape_cb_ops,		/* cb_ops */
	NULL,			/* bus_ops */
	NULL			/* power */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"LDI example driver",
	&vtape_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

int
_init(void)
{
	int             rv;

	cmn_err(CE_NOTE, "Inside init");

	if ((rv = ddi_soft_state_init(&vtape_statep, sizeof(vtape_state_t),
				      0)) != 0) {
		cmn_err(CE_WARN, "vtape _init: soft state init failed\n");
		return (rv);
	}
	if ((rv = mod_install(&modlinkage)) != 0) {
		cmn_err(CE_WARN, "vtape _init: mod_install failed\n");
		goto FAIL;
	}
	return (rv);
	/* NOTEREACHED */
FAIL:
	ddi_soft_state_fini(&vtape_statep);
	return (rv);
}

int
_info(struct modinfo * modinfop)
{
	cmn_err(CE_NOTE, "Inside info");
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int             rv;
	cmn_err(CE_NOTE, "Inside fini");
	if ((rv = mod_remove(&modlinkage)) != 0) {
		return (rv);
	}
	ddi_soft_state_fini(&vtape_statep);
	return (rv);
}

/*
 * 1:1 mapping between minor number and instance
 */

static int
vtape_info(dev_info_t * dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int64_t         inst;
	minor_t         minor;
	vtape_state_t    *statep;
	char           *myname = "vtape_info";
	minor = getminor((dev_t) arg);
	inst = minor;
	cmn_err(CE_NOTE, "Inside vtape_info");
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		statep = ddi_get_soft_state(vtape_statep, inst);
		if (statep == NULL) {
			cmn_err(CE_WARN, "%s: get soft state "
				"failed on inst %d\n", myname, inst);
			return (DDI_FAILURE);
		}
		*result = (void *) statep->dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *) inst;
		break;
	default:
		break;
	}
	return (DDI_SUCCESS);
}

static int
vtape_attach(dev_info_t * dip, ddi_attach_cmd_t cmd)
{
	int             inst;
	vtape_state_t    *statep;
	char           *myname = "vtape_attach";
	cmn_err(CE_NOTE, "Inside vtape_attach");
	switch (cmd) {
	case DDI_ATTACH:
		inst = ddi_get_instance(dip);
		if (ddi_soft_state_zalloc(vtape_statep, inst) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s: ddi_soft_state_zallac failed "
				"on inst %d\n", myname, inst);
			goto FAIL;
		}
		statep = (vtape_state_t *) ddi_get_soft_state(vtape_statep, inst);
		if (statep == NULL) {
			cmn_err(CE_WARN, "%s: ddi_get_soft_state failed on "
				"inst %d\n", myname, inst);
			goto FAIL;
		}
		statep->dip = dip;
		statep->minor = inst;
		statep->rec_size = MIN_REC_SIZE;
		if (ddi_create_minor_node(dip, "node", S_IFCHR, statep->minor,
					  DDI_PSEUDO, 0) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s: ddi_create_minor_node failed on "
				"inst %d\n", myname, inst);
			goto FAIL;
		}
		if (ddi_create_minor_node(dip, "rnode", S_IFCHR, statep->minor, DDI_PSEUDO, 0) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s: ddi_create_minor_node failed on "
				"inst %d\n", myname, inst);
			goto FAIL;
		}
		mutex_init(&statep->lock, NULL, MUTEX_DRIVER, NULL);
		return (DDI_SUCCESS);
	case DDI_RESUME:
	case DDI_PM_RESUME:
	default:
		break;
	}
	return (DDI_FAILURE);
	/* NOTREACHED */
FAIL:
	ddi_soft_state_free(vtape_statep, inst);
	ddi_remove_minor_node(dip, NULL);
	return (DDI_FAILURE);
}

static int
vtape_detach(dev_info_t * dip, ddi_detach_cmd_t cmd)
{
	int             inst;
	vtape_state_t    *statep;
	char           *myname = "vtape_detach";
	inst = ddi_get_instance(dip);
	statep = ddi_get_soft_state(vtape_statep, inst);
	cmn_err(CE_NOTE, "Inside vtape_detach");
	if (statep == NULL) {
		cmn_err(CE_WARN, "%s: get soft state failed on "
			"inst %d\n", myname, inst);
		return (DDI_FAILURE);
	}
	if (statep->dip != dip) {
		cmn_err(CE_WARN, "%s: soft state does not match devinfo "
			"on inst %d\n", myname, inst);
		return (DDI_FAILURE);
	}
	switch (cmd) {
	case DDI_DETACH:
		mutex_destroy(&statep->lock);
		ddi_soft_state_free(vtape_statep, inst);
		ddi_remove_minor_node(dip, NULL);
		return (DDI_SUCCESS);
	case DDI_SUSPEND:
	case DDI_PM_SUSPEND:
	default:
		break;
	}
	return (DDI_FAILURE);
}

/*
 * Here in open, we open the target specified by a property and
 * store the layered handle and ident in our soft state. a good target would
 * be "/dev/console" or more interestingly, a pseudo terminal as specified by
 * the tty command
 */
/* ARGSUSED */

static int
vtape_open(dev_t * devtp, int oflag, int otyp, cred_t * credp)
{
	int             rv, inst = getminor(*devtp);
	vtape_state_t    *statep;
	char           *myname = "vtape_open";
	dev_info_t     *dip;
	char           *vtape_targ = NULL;
	statep = (vtape_state_t *) ddi_get_soft_state(vtape_statep, inst);
	cmn_err(CE_NOTE, "Inside vtape_open");
	if (statep == NULL) {
		cmn_err(CE_WARN, "%s: ddi_get_soft_state failed on "
			"inst %d\n", myname, inst);
		return (EIO);
	}
	dip = statep->dip;
	/*
	 * our target device to open should be specified by the "vtape_targ"
	 * string property, which should be set in this driver's .conf file
	 */
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_NOTPROM,
			       "vtape_targ", &vtape_targ) != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "%s: ddi_prop_lookup_string failed on "
			"inst %d\n", myname, inst);
		return (EIO);
	}
	/*
	 * since we only have one pair of lh and li available, we
	 * do not allow multiple on the same instance
	 */
	mutex_enter(&statep->lock);
	if (statep->flags & (vtape_OPENED | vtape_IDENTED)) {
		cmn_err(CE_WARN, "%s: multiple layered opens or idents "
			"from inst %d not allowed\n", myname, inst);
		mutex_exit(&statep->lock);
		ddi_prop_free(vtape_targ);
		return (EIO);
	}
	rv = ldi_ident_from_dev(*devtp, &statep->li);
	if (rv != 0) {
		cmn_err(CE_WARN, "%s: ldi_ident_from_dev failed on inst %d\n",
			myname, inst);
		goto FAIL;
	}
	statep->flags |= vtape_IDENTED;
	rv = ldi_open_by_name(vtape_targ, FREAD | FWRITE, credp, &statep->lh,
			      statep->li);
	if (rv != 0) {
		cmn_err(CE_WARN, "%s: ldi_open_by_name failed on inst %d\n",
			myname, inst);
		goto FAIL;
	}
	statep->flags |= vtape_OPENED;
	cmn_err(CE_CONT, "\n%s: opened target %s successfully on inst %d\n",
		myname, vtape_targ, inst);
	rv = 0;
FAIL:
	/* cleanup on error */
	if (rv != 0) {
		if (statep->flags & vtape_OPENED)
			(void) ldi_close(statep->lh, FREAD | FWRITE, credp);
		if (statep->flags & vtape_IDENTED)
			ldi_ident_release(statep->li);
		statep->flags &= ~(vtape_OPENED | vtape_IDENTED);
	}
	statep->ioctl_flag = 0;
	mutex_exit(&statep->lock);
	if (vtape_targ != NULL)
		ddi_prop_free(vtape_targ);
	return (rv);
}

/*
 * Here in close, we close the target indicated by the lh member
 * in our soft state and release the ident, li as well. in fact, we MUST do
 * both of these at all times even if close yields an error because the
 * device framework effectively closes the device, releasing all data
 * associated with it and simply returning whatever value the target's
 * close(9E) returned. therefore, we must as well.
 */
/* ARGSUSED */

static int
vtape_close(dev_t devt, int oflag, int otyp, cred_t * credp)
{
	int             rv, inst = getminor(devt),count=0;
	vtape_state_t    *statep;
	char           *myname = "vtape_close";
	statep = (vtape_state_t *) ddi_get_soft_state(vtape_statep, inst);
	cmn_err(CE_NOTE, "Inside vtape_close");
	if (statep == NULL) {
		cmn_err(CE_WARN, "%s: ddi_get_soft_state failed on "
			"inst %d\n", myname, inst);
		return (EIO);
	}
	mutex_enter(&statep->lock);
	rv = ldi_close(statep->lh, FREAD | FWRITE, credp);
	if (rv != 0) {
		cmn_err(CE_WARN, "%s: ldi_close failed on inst %d, but will ",
			"continue to release ident\n", myname, inst);
	}
	ldi_ident_release(statep->li);
	if (rv == 0) {
		cmn_err(CE_CONT, "\n%s: closed target successfully on "
			"inst %d\n", myname, inst);
	}
	if (closeflag) {
		statep->files[fileno].no_of_rec = j;
		fileno++;
		j = 0;
		closeflag = 0;
		fileflag = 1;
		readfileno = fileno;
		statep->write_oft = oft;
		statep->read_oft = read_oft = oft;
		if (oft > statep->max_oft) {
			statep->max_oft = oft;
			statep->total_no_files += 1;
		}
	}
	if (readflag) {
		readflag = 0;
		if(statep->files[readfileno].eof_flag)
		{
		count= statep->files[readfileno].eof_flag;
		read_oft+=count;
                cmn_err(CE_NOTE, "first: read_oft=%d count=%d readfileno:%d", read_oft,count,readfileno);
		}
		oft = read_oft;
		statep->write_oft = oft;
		readfileno++;
		fileno = readfileno;
	}
	statep->flags &= ~(vtape_OPENED | vtape_IDENTED);
	mutex_exit(&statep->lock);
	return (rv);
}


static int
vtape_read(dev_t devt, struct uio * uiop, cred_t * credp)
{
	int             count=0, resid = 0, rv, inst = getminor(devt);
	vtape_state_t    *statep;
	char           *myname = "vtape_read";
	uint64_t        size;

	statep = (vtape_state_t *) ddi_get_soft_state(vtape_statep, inst);
	cmn_err(CE_NOTE, "Inside vtape_read");
	//cmn_err(CE_NOTE, "read uio buffer:%s", uiop->uio_iov->iov_base);
	if (statep == NULL) {
		cmn_err(CE_WARN, "%s: ddi_get_soft_state failed on "
			"inst %d\n", myname, inst);
		return (EIO);
	}
	cmn_err(CE_NOTE, "read uio buffer:%d", statep->VALIDATE_FLAG);
	
	if (statep->VALIDATE_FLAG == 1) {
		cmn_err(CE_NOTE,"inside validate");
		ldi_get_size(statep->lh, &size);
		while (uiop->uio_offset < (size - 512)) {
			uiop->uio_offset = size - 1024;
			uiop->uio_resid = 512;
			cmn_err(CE_NOTE, "inside validate. size of device: %d", size);
			rv = ldi_read(statep->lh, uiop, credp);

			cmn_err(CE_NOTE, "after ldi_read in validate return value:%d   offset value: %d   resid: %d", rv, uiop->uio_offset, uiop->uio_resid);
			statep->VALIDATE_FLAG = 0;
		}
	} else {

		if (statep->ERASE_FLAG ||  read_oft == statep->max_oft)
		return EIO;
	
		if (uiop->uio_offset < statep->max_oft * 512) {
			resid = uiop->uio_resid;
			uiop->uio_offset = read_oft * 512;
			//cmn_err(CE_NOTE, "first: read_oft= %d   write_oft= %d", read_oft, oft);
			rv = ldi_read(statep->lh, uiop, credp);
			read_oft += ((resid / 512) + 1);
			//cmn_err(CE_NOTE, "first: read_oft= %d   write_oft= %d", read_oft, oft);
		}
		
	}
	if (rv == 0)
		readflag = 1;
	return rv;
}


/* ARGSUSED */
static int
vtape_write(dev_t devt, struct uio * uiop, cred_t * credp)
{
	int             rv, i = 0, n = 1, inst = getminor(devt);
	vtape_state_t    *statep;
	char           *myname = "vtape_write";
	char            buf[512];
	uint64_t        size;

	statep = (vtape_state_t *) ddi_get_soft_state(vtape_statep, inst);
	cmn_err(CE_NOTE, "Inside vtape_write");

	if (statep == NULL) {
		cmn_err(CE_WARN, "%s: ddi_get_soft_state failed on "
			"inst %d\n", myname, inst);
		return (EIO);
	}
	statep->ERASE_FLAG = 0;

	if (statep->WRITE_HEADER_FLAG == 1) {
		ldi_get_size(statep->lh, &size);
		uiop->uio_offset = size - 1024;
		rv = copyin(uiop->uio_iov->iov_base, buf, 512);

		rv = ldi_write(statep->lh, uiop, credp);
		statep->WRITE_HEADER_FLAG = 0;
		return rv;
	} else {
		oft = statep->write_oft;
		uiop->uio_offset = oft * 512;
		i = uiop->uio_resid / 512;

		//cmn_err(CE_NOTE, "Before: off: %d	 res: %d", uiop->uio_offset, uiop->uio_resid);
		j++;

		if (fileflag) {
			copyin(uiop->uio_iov->iov_base, statep->files[fileno].filename, 20);

			fileflag = 0;
			statep->files[fileno].offset = oft;
			closeflag = 1;
		}
		cmn_err(CE_NOTE, "1: ARRAY before calling ldi_write. IOVEC buffer contents: %s   offset value: %d    residual count: %d   J: %d   a[fileno]: %d", uiop->uio_iov->iov_base, uiop->uio_offset, uiop->uio_resid, j, a[fileno]);

		rv = ldi_write(statep->lh, uiop, credp);

		oft++;
		oft += i;
		cmn_err(CE_NOTE, "2: ARRAY after calling ldi_write. offset value: %d	residual count: %d   j: %d    a[fileno]: %d", uiop->uio_offset, uiop->uio_resid, j, a[n]);
		statep->write_oft = oft;
	}
	return rv;
}


static int
vtape_ioctl(dev_t devt, int cmd, intptr_t arg, int mode,
	  cred_t * credp, int *rval_p)
{

	int             options, rv = 0, i = 0, count = 0, inst = getminor(devt);
	vtape_state_t    *statep;
	
	struct mtlop    vtapemt;		
	struct mtget    vtapemtget;			//structure for MTIOCGET-mag tape get status command
	struct mtdrivetype vtapemtconfig;		//struct for    MTIOCGETDRIVETYPE - get tape config data
	

	statep = (vtape_state_t *) ddi_get_soft_state(vtape_statep, inst);
	if (statep == NULL) {
		cmn_err(CE_WARN, "vtape_ioctl: ddi_get_soft_state failed on "
			"inst %d\n", inst);
		return (EIO);
	}
	//cmn_err(CE_NOTE, "cmd:%c cmd:%d arg:%d ", cmd, cmd, arg);

	switch (cmd) {
	case WRITE_HEADER:
		cmn_err(CE_NOTE, "Inside ioctl write header!!");
		statep->WRITE_HEADER_FLAG = 1;
		break;
	case VALIDATE:
		statep->VALIDATE_FLAG = 1;
		cmn_err(CE_NOTE, "Inside ioctl validate header!!");
		break;
	case REWIND:
		statep->REWIND_FLAG = 0;
		cmn_err(CE_NOTE, "Inside rewind ioctl!!");
		break;
	case NON_REWIND:
		cmn_err(CE_NOTE, "Inside non rewind ioctl!!");
		break;
	case MTIOCLTOP:
		cmn_err(CE_NOTE, "Inside MTIOCLTOP!!");
		ddi_copyin((void *) arg, &vtapemt, sizeof(struct mtlop), mode);
		options = vtapemt.mt_op;
		count = vtapemt.mt_count;
		//cmn_err(CE_NOTE, "cmd:%d count %d", vtapemt.mt_op, vtapemt.mt_count);
		switch (options) {

		case MTFSF:

			if ((readfileno + count) >= statep->total_no_files) {
				cmn_err(CE_NOTE, "Error: fsf fails!!");
				return EIO;
				
			} else {
				readfileno += count;
				oft = read_oft = (statep->files[readfileno].offset);
				fileno = readfileno;

			}
			cmn_err(CE_NOTE, "Hello I am in ioctl fsf: read_oft: %d   readfileno: %d", read_oft, readfileno);
			break;
		case MTBSF:
			readfileno -= count;
			cmn_err(CE_NOTE, "Hello I am in ioctl bsf!! readfileno: %d", readfileno);
			if (readfileno >= 0) {
				oft = read_oft = (statep->files[readfileno].offset);
				fileno = readfileno;
				statep->write_oft = oft;
			} else {
				readfileno += count;
				cmn_err(CE_NOTE, "Error: bsf fails!!");
				return EIO;
				
			}
			cmn_err(CE_NOTE, "Hello I am in ioctl bsf!! read_oft:%d bsfreadfieno:%d", read_oft, readfileno);
			break;
		case MTFSR:
			cmn_err(CE_NOTE, "Hello I am in ioctl fsr!!");
			break;
		case MTBSR:
			cmn_err(CE_NOTE, "Hello I am in ioctl bsr!!");
			break;
		case MTREW:
			cmn_err(CE_NOTE, "Hello I am in ioctl rewind!!");
			read_oft = oft = statep->write_oft = 0;
			readfileno = fileno = 0;
			break;
		case MTERASE:
			read_oft = oft = statep->write_oft = statep->max_oft=0;
			for (i = 0; i < fileno; i++) {
				copyout(NULL, statep->files[fileno].filename, 20);
				statep->files[fileno].offset = 0;
				statep->files[fileno].no_of_rec = 0;
			}
			fileno = 0;
			readfileno = 0;
			statep->ERASE_FLAG = 1;
			cmn_err(CE_NOTE, "Hello I am in ioctl erase!!");
			break;
		case MTTELL:
			vtapemt.mt_op = MTTELL;
			vtapemt.mt_count = oft;
			ddi_copyout(&vtapemt, (void *) arg, sizeof(struct mtlop), mode);
			cmn_err(CE_NOTE, "Hello I am in ioctl erase!! offset:%d", oft);
			break;
		case MTSEEK:
			oft = count;
			read_oft = count;
			while (count >= statep->files[i].offset)
				i++;
			fileno = readfileno = i - 1;
			statep->write_oft = oft;
			vtapemt.mt_op = MTTELL;
			ddi_copyout(&vtapemt, (void *) arg, sizeof(struct mtlop), mode);
			cmn_err(CE_NOTE, "Hello I am in ioctl mtseek!!  offset: %d   read oft: %d   fileno: %d", oft, read_oft, fileno);
			break;
		case MTSRSZ:
			if (count < MIN_REC_SIZE) {
				return (EINVAL);
			}
			if (count > MAX_REC_SIZE) {
				return (EINVAL);
			}
			if (count % MIN_REC_SIZE != 0) {
				return (EINVAL);
			}
			statep->rec_size = count;
			break;
		case MTGRSZ:
			vtapemt.mt_count = statep->rec_size;
			ddi_copyout(&vtapemt, (void *) arg, sizeof(struct mtlop), mode);
			break;
		case MTWEOF:
			cmn_err(CE_NOTE, "Hello I am in ioctl mteof!!\n");
			if (count == 0) {
				return EIO;
			}
			statep->files[fileno - 1].eof_flag = count;
			oft += count;
			statep->write_oft = oft;
			read_oft = oft;
			cmn_err(CE_NOTE, "flag: %d   fileno: %d   offset value: %d\n", statep->files[fileno - 1].eof_flag, fileno, oft);
			break;

		default:
			cmn_err(CE_NOTE, "Hello I am in ioctl default in MTIOCLTOP!!");
		}
		break;

	case MTIOCGET:
		cmn_err(CE_NOTE, "Hello I am in ioctl status!!");
		ddi_copyin((void *) arg, &vtapemtget, sizeof(struct mtget), mode);
		vtapemtget.mt_type = 0;	/* type of magtape device */

		/* the following two registers are grossly device dependent */
		vtapemtget.mt_dsreg = 0;	/* ``drive status'' register */
		vtapemtget.mt_erreg = 0;	/* ``error'' register */

		/* optional error info. */
		vtapemtget.mt_resid = 0;	/* residual count */
		vtapemtget.mt_fileno = 0;	/* file number of current position */
		vtapemtget.mt_blkno = oft;	/* block number of current
						 * position */
		vtapemtget.mt_flags = 0;
		vtapemtget.mt_bf = 0;		/* optimum blocking factor */
		ddi_copyout(&vtapemtget, (void *) arg, sizeof(struct mtget), mode);
		break;

	case MTIOCGETDRIVETYPE:
		cmn_err(CE_NOTE, "Hello I am in ioctl config!!");
		(void) snprintf(vtapemtconfig.name, 13, "%s", "virtual tape");	/* Name, for debug */
		(void) snprintf(vtapemtconfig.vid, 7, "%s", "krcube");	/* Vendor id and model
									 * (product) id */
		vtapemtconfig.type = 'V';	/* Drive type for driver */
		vtapemtconfig.bsize = 512;	/* Block size */
		vtapemtconfig.options = 2;	/* Drive options */
		vtapemtconfig.max_rretries = 1;	/* Max read retries */
		vtapemtconfig.max_wretries = 1;	/* Max write retries */
		(void) snprintf(vtapemtconfig.densities, 5, "%s", "high");	/* density codes,
										 * low->hi */
		vtapemtconfig.default_density = 'm';	/* Default density
							 * chosen */
		(void) snprintf(vtapemtconfig.speeds, 5, "%s", "high");	/* speed codes, low->hi */
		vtapemtconfig.non_motion_timeout = 60;	/* Inquiry type commands */
		vtapemtconfig.io_timeout = 60;		/* io timeout. seconds */
		vtapemtconfig.rewind_timeout = 60;	/* rewind timeout.
							 * seconds */
		vtapemtconfig.space_timeout = 60;	/* space cmd timeout. seconds */
		vtapemtconfig.load_timeout = 60;	/* load tape time in seconds */
		vtapemtconfig.unload_timeout = 60;	/* Unload tape time in
							 * scounds */
		vtapemtconfig.erase_timeout = 60;
		ddi_copyout(&vtapemtconfig, (void *) arg, sizeof(struct mtdrivetype), mode);
		break;
	default:
		cmn_err(CE_NOTE, "Hello I am in default!!");
	}
	return (*rval_p);
}
