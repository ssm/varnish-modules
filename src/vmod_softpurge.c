#include "vrt.h"
#include "vcl.h"
#include "cache/cache.h"
#include "hash/hash_slinger.h"
#include "config.h"

#include "vcc_softpurge_if.h"

void
vmod_softpurge(VRT_CTX)
{
	struct objcore *oc, **ocp;
	struct objhead *oh;
	unsigned spc, nobj, n;
	double now;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (ctx->method != VCL_MET_HIT && ctx->method != VCL_MET_MISS) {
		VSLb(ctx->vsl, SLT_VCL_Error, "softpurge() is only "
		    "available in vcl_hit{} and vcl_miss{}");
		return;
	}

	CHECK_OBJ_NOTNULL(ctx->req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->req->wrk, WORKER_MAGIC);
	oh = ctx->req->objcore->objhead;
	CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
	spc = WS_Reserve(ctx->ws, 0);
	assert(spc >= sizeof *ocp);

	nobj = 0;
	ocp = (void*)ctx->ws->f;
	now = ctx->req->t_prev;
	Lck_Lock(&oh->mtx);
	assert(oh->refcnt > 0);
	VTAILQ_FOREACH(oc, &oh->objcs, list) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
		assert(oc->objhead == oh);
		if (oc->flags & OC_F_BUSY)
			continue;
		if (oc->exp_flags & OC_EF_DYING)
			continue;
		if (spc < sizeof *ocp)
			break;
		oc->refcnt++;
		spc -= sizeof *ocp;
		ocp[nobj++] = oc;
	}
	Lck_Unlock(&oh->mtx);

	for (n = 0; n < nobj; n++) {
		oc = ocp[n];
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
#ifdef VARNISH_PLUS
		/* Varnish Plus interface for EXP_Rearm() is different. */
		EXP_Rearm(ctx->req->wrk, oc, now, 0, oc->exp.grace, oc->exp.keep);
#else
		EXP_Rearm(oc, now, 0, oc->exp.grace, oc->exp.keep);
#endif
		(void)HSH_DerefObjCore(ctx->req->wrk, &oc);

	}
	WS_Release(ctx->ws, 0);
}
