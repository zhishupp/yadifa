/*------------------------------------------------------------------------------
 *
 * Copyright (c) 2011-2016, EURid. All rights reserved.
 * The YADIFA TM software product is provided under the BSD 3-clause license:
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *        * Redistributions of source code must retain the above copyright 
 *          notice, this list of conditions and the following disclaimer.
 *        * Redistributions in binary form must reproduce the above copyright 
 *          notice, this list of conditions and the following disclaimer in the 
 *          documentation and/or other materials provided with the distribution.
 *        * Neither the name of EURid nor the names of its contributors may be 
 *          used to endorse or promote products derived from this software 
 *          without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *------------------------------------------------------------------------------
 *
 */
/** @defgroup nsec3 NSEC3 functions
 *  @ingroup dnsdbdnssec
 *  @brief
 *
 *
 *
 * @{
 */
/*------------------------------------------------------------------------------
 *
 * USE INCLUDES */
#include "dnsdb/dnsdb-config.h"
#include <stdio.h>
#include <stdlib.h>

#include <dnscore/typebitmap.h>

#include "dnsdb/zdb_types.h"
#include "dnsdb/zdb_zone_label_iterator.h"
#include "dnsdb/zdb_record.h"

#include "dnsdb/nsec3_update.h"
#include "dnsdb/nsec_common.h"

#include "dnsdb/nsec3_owner.h"

#include "dnsdb/rrsig.h"

#include "dnsdb/zdb_listener.h"
#include "dnsdb/zdb_icmtl.h"

#define MODULE_MSG_HANDLE g_dnssec_logger

extern logger_handle *g_dnssec_logger;

#ifndef DEBUG
#undef NSEC3_UPDATE_ZONE_DEBUG
#define NSEC3_UPDATE_ZONE_DEBUG 0
#endif

ya_result
nsec3_chain_callback_nop(const zdb_zone *zone, s8 chain_index, void *args)
{
    (void)zone;
    (void)chain_index;
    (void)args;
    return SUCCESS;
}

/*
 * Takes the result of an update and commits it to the label
 */

void
nsec3_update_rrsig_commit(zdb_packed_ttlrdata *removed_rrsig_sll, zdb_packed_ttlrdata *added_rrsig_sll, nsec3_zone_item *item, zdb_zone *zone)
{
    /*
     * NOTE: NSEC3 records have no associated label. (Not really, not in the zone-contained-label sense from the DB )
     *
     *
     *       I have all the information available in the zone item.
     */

    zdb_listener_notify_update_nsec3rrsig(zone, removed_rrsig_sll, added_rrsig_sll, item);

    zdb_packed_ttlrdata *sig;
    zdb_packed_ttlrdata **rrsig_sllp = &item->rrsig;

    /*
     * For each removed signature:
     *
     * Find it in the label's RRSIG list, then remove it:
     * ZFREE + MFREE
     *
     */

    sig = removed_rrsig_sll;

    while(sig != NULL)
    {
        /*
         * Look for the RRSIG
         *
         */

        zdb_packed_ttlrdata **rrsig_recordp = rrsig_sllp;
        zdb_packed_ttlrdata *rrsig_record = *rrsig_recordp;
        /* This is why my "next" pointer is ALWAYS the first field */
        
#ifdef DEBUG
        rdata_desc rdatadesc = {TYPE_RRSIG, ZDB_PACKEDRECORD_PTR_RDATASIZE(sig), ZDB_PACKEDRECORD_PTR_RDATAPTR(sig)};
        log_debug("rrsig: deleting: %{digest32h} %{typerdatadesc}", item->digest, &rdatadesc);
#endif

        while(rrsig_record != NULL)
        {
            /*
             * Check if the COVERED TYPE + TAG are matching
             */

            if(ZDB_PACKEDRECORD_PTR_RDATASIZE(sig) == ZDB_PACKEDRECORD_PTR_RDATASIZE(sig))
            {
                if(memcmp(ZDB_PACKEDRECORD_PTR_RDATAPTR(rrsig_record), ZDB_PACKEDRECORD_PTR_RDATAPTR(sig), RRSIG_RDATA_HEADER_LEN) == 0)
                {
                    /*
                    u16 type = RRSIG_TYPE_COVERED(*sig);
                    log_debug("rrsig_update_commit : '%{dnsname}' removing RRSIG %{dnstype}",label->name,&type);
                     */

                    *rrsig_recordp = rrsig_record->next;
    #ifdef DEBUG
                    rrsig_record->next = (zdb_packed_ttlrdata*)~0;
    #endif
                    ZDB_RECORD_ZFREE(rrsig_record);

                    /*
                     * I can stop here.
                     */

                    break;
                }
            }

            rrsig_recordp = &rrsig_record->next;
            rrsig_record = *rrsig_recordp;
        }

        zdb_packed_ttlrdata* tmp = sig;
        sig = sig->next;
        free(tmp);
    }

    /*
     * For each added signature:
     *
     * Add it:
     *
     * ZFREE + MFREE
     *
     */

    sig = added_rrsig_sll;

    while(sig != NULL)
    {
        zdb_packed_ttlrdata* rrsig_record;

        /*
        u16 type = RRSIG_TYPE_COVERED(*sig);
        log_debug("rrsig_update_commit : '%{dnsname}' adding RRSIG %{dnstype}",label->name,&type);
         */

#ifdef DEBUG
        rdata_desc rdatadesc={TYPE_RRSIG, ZDB_PACKEDRECORD_PTR_RDATASIZE(sig), ZDB_PACKEDRECORD_PTR_RDATAPTR(sig)};
        log_debug("rrsig: adding: %{digest32h} %{typerdatadesc}", item->digest, &rdatadesc);
#endif

        ZDB_RECORD_ZALLOC(rrsig_record, sig->ttl, ZDB_PACKEDRECORD_PTR_RDATASIZE(sig), ZDB_PACKEDRECORD_PTR_RDATAPTR(sig));

        rrsig_record->next = *rrsig_sllp;
        *rrsig_sllp = rrsig_record;

        zdb_packed_ttlrdata* tmp = sig;
        sig = sig->next;
        free(tmp);
    }



}

bool
nsec3_is_label_covered(const zdb_rr_label *label, bool opt_out)
{
    bool opt_in = !opt_out;
    bool skip_children = FALSE;    
    bool nsec3_covered = FALSE;
    //bool force_rrsig = FALSE;
    

    
    if(!ZDB_LABEL_ISAPEX(label)) /* Not the origin (The apex of a zone has got a '.' label */
    {
        if(ZDB_LABEL_ATDELEGATION(label))
        {
            skip_children = TRUE;


            
            bool has_ds = zdb_record_find(&label->resource_record_set, TYPE_DS) != NULL;
            

            
            nsec3_covered = opt_in|has_ds;
            //force_rrsig = has_ds;            

            /*
             * After processing this node, the brother will be processed.
             */
        }
        else if(ZDB_LABEL_UNDERDELEGATION(label))
        {
            /* An empty non-terminal must only be signed if it does not end on a non-secure delegation */
            
            nsec3_covered = false;
            skip_children = true;
            //force_rrsig = false;
        }
        else
        {
            /* An empty non-terminal must only be signed if it does not end on a non-secure delegation */
            
            bool notempty = !zdb_record_isempty(&label->resource_record_set);
            
            nsec3_covered = notempty;
        }
    }
    else
    {
        /*
         * We are at the origin:
         *
         * Records => RRSIG
         */

        return TRUE;
    }
    


    if(!skip_children)
    {
        /* for all children */
        
        dictionary_iterator iter;
        

        
        dictionary_iterator_init(&label->sub, &iter);
        

        
        while(!nsec3_covered && dictionary_iterator_hasnext(&iter))
        {
            zdb_rr_label *sub_label =  *(zdb_rr_label**)dictionary_iterator_next(&iter);
            /* if a child has been signed, then this one will be too */

            nsec3_covered |= nsec3_is_label_covered(sub_label, opt_out);
        }
    }
    

    
    /*
     * If it's opt-out and we are not forced to sign, then skip to the next one
     */

    if(opt_out && !nsec3_covered)
    {
        // n3 = n3->next;
        
        return FALSE;
    }
    
    return nsec3_covered;
}

typedef struct nsec3_update_zone_nsec3_nodes_recursive_args nsec3_update_zone_nsec3_nodes_recursive_args;

struct nsec3_update_zone_nsec3_nodes_recursive_args
{
    zdb_zone* zone;
    s32 label_stack_level;
    u32 origin_len;
    u32 min_ttl;
    u8 nsec3_flags;
    bool opt_out;
    zdb_rr_label *label_stack[128];
    u8 name[2 + MAX_DOMAIN_LENGTH];
    u8 digest[1 + MAX_DIGEST_LENGTH];
    
    u32 internal_statistics_label_count;
    u32 internal_statistics_delegation_count;
    u32 internal_statistics_nsec3_count;
    
    type_bit_maps_context type_context;
};

static bool
nsec3_update_label_nsec3_nodes_recursive(nsec3_update_zone_nsec3_nodes_recursive_args *commonargs)
{
    /* retrieve context */
    
    zdb_zone* zone = commonargs->zone;
    s32 label_stack_level = commonargs->label_stack_level;
    u32 origin_len = commonargs->origin_len;
    bool opt_out = commonargs->opt_out;
    bool opt_in = !opt_out;
    zdb_rr_label **label_stack = &commonargs->label_stack[0];
    u8 *name = &commonargs->name[0];
    u8 *digest = &commonargs->digest[0];
    type_bit_maps_context *type_context = &commonargs->type_context;
    u8 nsec3_flags = commonargs->nsec3_flags;
    u32 min_ttl = commonargs->min_ttl;
    
    /* build the current name */
    
#if NSEC3_UPDATE_ZONE_DEBUG
    u8 debug_name[MAX_DOMAIN_LENGTH];
    {   
        u8 *p = debug_name;

        for(s32 sp = label_stack_level; sp > 0; sp--)
        {
            u8 *q = label_stack[sp]->name;
            u8 len = *q + 1;
            memcpy(p, q, len);
            p += len;
        }
        
        memcpy(p, zone->origin, origin_len);
        
        
        log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : enter", label_stack_level, debug_name);

    }
#endif   

    bool skip_children = FALSE;
    bool nsec3_covered = FALSE;
    bool force_rrsig   = TRUE;

    zdb_rr_label *label = label_stack[label_stack_level];

    commonargs->internal_statistics_label_count++;

    yassert((label->flags & ZDB_RR_LABEL_NSEC) == 0);   /* Bad, this should not be called on an NSEC zone */

    /*
     * First check the delegation
     */

    if(!ZDB_LABEL_ISAPEX(label)) /* Not the origin (The apex of a zone has got a '.' label */
    {
        if(ZDB_LABEL_ATDELEGATION(label))
        {
            /**
             * Delegation.
             */
            
#if NSEC3_UPDATE_ZONE_DEBUG
            log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : delegation", label_stack_level, debug_name);
#endif
            
            commonargs->internal_statistics_delegation_count++;

            skip_children = TRUE;

            bool has_ds = zdb_record_find(&label->resource_record_set, TYPE_DS) != NULL;
            
            nsec3_covered = opt_in|has_ds;
            force_rrsig = has_ds;            

            /*
             * After processing this node, the brother will be processed.
             */
        }
        else if(ZDB_LABEL_UNDERDELEGATION(label))
        {
            nsec3_covered = FALSE;
            force_rrsig = FALSE;
            skip_children = TRUE;
        }
        else
        {
            /* An empty non-terminal must only be signed if it does not end on a non-secure delegation */
            
            bool notempty = !zdb_record_isempty(&label->resource_record_set);
            
#if NSEC3_UPDATE_ZONE_DEBUG
            log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : %s", label_stack_level, debug_name, (notempty)?"not empty":"empty");
#endif
            
            nsec3_covered = notempty;
            
            force_rrsig = notempty;
        }
    }
    else
    {
        /*
         * We are at the origin:
         *
         * Records => RRSIG
         */

        nsec3_covered = TRUE;
        
#if NSEC3_UPDATE_ZONE_DEBUG
        log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : apex", label_stack_level, debug_name);
#endif
    }

    if(!skip_children)
    {
        /* for all children */
        
        commonargs->label_stack_level++;
        
        dictionary_iterator iter;
        dictionary_iterator_init(&label->sub, &iter);
        
        while(dictionary_iterator_hasnext(&iter))
        {
            zdb_rr_label *sub_label =  *(zdb_rr_label**)dictionary_iterator_next(&iter);
            /* if a child has been signed, then this one will be too */
            
            commonargs->label_stack[commonargs->label_stack_level] = sub_label;
            
            nsec3_covered |= nsec3_update_label_nsec3_nodes_recursive(commonargs);
        }
        
        commonargs->label_stack_level--;
    }
    
    /*
     * If it's opt-out and we are not forced to sign, then skip to the next one
     */

    if(opt_out && !nsec3_covered)
    {
        // n3 = n3->next;
        
#if NSEC3_UPDATE_ZONE_DEBUG
        log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : exit", label_stack_level, debug_name);
#endif
        
        return FALSE;
    }
    
#if NSEC3_UPDATE_ZONE_DEBUG
    log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : add", label_stack_level, debug_name);
#endif
    
    u32 name_len;
    
    {    
        u8 *p = name;

        for(s32 sp = label_stack_level; sp > 0; sp--)
        {
            u8 *q = label_stack[sp]->name;
            u8 len = *q + 1;
            memcpy(p, q, len);
            p += len;
        }
        
        memcpy(p, zone->origin, origin_len);
        
        name_len = (p - name) + origin_len;
    }
        
    /*
     * This label can now be processed for NSEC3
     */

    u16 type_bit_maps_size = type_bit_maps_initialize(type_context, label, FALSE, force_rrsig);

    nsec3_zone* n3 = zone->nsec.nsec3;

    nsec3_label_extension* n3ext = label->nsec.nsec3;

    /*
     * n3ext will be NULL if the zone is marked as loading and labels are
     * added after the NSEC3PARAM
     *
     * (loading is an optimisation that I'm setting up)
     */

    /*
     * Create all missing NSEC3 extensions for the label
     * Each NSEC3PARAM is the head of an NSEC3 chain
     * They are stored in a linked list in the zone
     * Each label that is nsec3 covered has a non-null nsec3 label extension that is a linked list to reference nsec3 items
     * The position in the NSEC3PARAM list must match the position in the nsec3 label extension list
     * 
     * New nodes are append at the end of the list
     * The internal resolver will only ever use the first item of the list
     */

    if(n3ext == NULL)
    {
        nsec3_label_extension* n3ext_first = NULL;

        do
        {
            n3ext = nsec3_label_extension_alloc();
            n3ext->self = NULL;
            n3ext->star = NULL;
            n3ext->next = n3ext_first; // add this (potentially empty) node in the nsec3 chain

            n3ext_first = n3ext;

            n3 = n3->next;
        }
        while(n3 != NULL);

        label->nsec.nsec3 = n3ext_first;
        label->flags |= ZDB_RR_LABEL_NSEC3;

        n3ext = n3ext_first;

        n3 = zone->nsec.nsec3;
    }

    /* For each NSEC3PARAM */        

    do
    {
        yassert(n3ext != NULL); /* The label is supposed to be ready */

        /*
            * If the NSEC3 extension has not been set up yet
            */

        if(n3ext->self == NULL)
        {
            /*
             * Compute the digest for this fqdn
             */

            nsec3_compute_digest_from_fqdn_with_len(n3, name, name_len, digest, FALSE);

            commonargs->internal_statistics_nsec3_count++;

#if NSEC3_UPDATE_ZONE_DEBUG!=0
            log_debug("nsec3: made '%{dnsname}' %{digest32h} ", name, digest);
#endif
            /*
             * DYNUPDATE:
             *
             * Seek for digest
             *
             * If the digest does not exists:
             *	Get the predecessor.
             *      If the predecessor is not marked:
             *	    Mark the predecessor for future add and output it right now
             *
             * Find the node with the computed digest
             */

            nsec3_zone_item* node;

            node = nsec3_avl_find(&n3->items, digest);

            if(node != NULL)
            {
                /*
                    * If the node exists, get the previous node and mark it for incremental delete
                    * ( I don't remember why I do this )
                    */

                nsec3_zone_item* node_prev = nsec3_avl_node_mod_prev(node);

                if((node_prev->flags & NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD) == 0)
                {
                    zdb_listener_notify_remove_nsec3(zone, node_prev, n3, min_ttl);
                    node_prev->flags |= NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD;
                }
            }
            else
            {
                /*
                    * Insert the node for that digest and mark it for incremental add
                    */

                node = nsec3_avl_insert(&n3->items, digest);

                node->flags |= NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD;
            }

            /*
                * Sets the nsec3 -> owner label link
                */

            nsec3_add_owner(node, label);

            /*
                * The self is edited later
                */

            node->flags |= nsec3_flags;

            /*
                * Update (or create) the bitmap of the types
                */

            if(node->type_bit_maps_size == 0)
            {
                /*
                    * Create the bitmap
                    */

                node->type_bit_maps_size = type_bit_maps_size;

                if(type_bit_maps_size > 0)
                {
                    /* LOCK */
                    ZALLOC_ARRAY_OR_DIE(u8*, node->type_bit_maps, type_bit_maps_size, NSEC3_TYPEBITMAPS_TAG);
                    /* UNLOCK */

                    type_bit_maps_write(node->type_bit_maps, type_context);
                }
            }
            else
            {
                /* Merge the existing bitmap with the new one */

                u8* tmp_type_bit_maps;
                
#if DNSCORE_HAS_ZALLOC_SUPPORT
                /* LOCK */
                ZALLOC_ARRAY_OR_DIE(u8*, tmp_type_bit_maps, type_bit_maps_size, NSEC3_TYPEBITMAPS_TAG);
                /* UNLOCK */
#else
                // zalloc can allocate 0 bytes (8 are allocated)
                // malloc cannot (undefined)
                ZALLOC_ARRAY_OR_DIE(u8*, tmp_type_bit_maps, MAX(type_bit_maps_size,1), NSEC3_TYPEBITMAPS_TAG);
#endif
                
                type_bit_maps_write(tmp_type_bit_maps, type_context);

                if(type_bit_maps_merge(type_context, node->type_bit_maps, node->type_bit_maps_size, tmp_type_bit_maps, type_bit_maps_size))
                {
                    /**
                     * TRUE : a merge occurred
                     * NOTE : this case never occurred while testing.  It has
                     * to be triggered with a dynupdate or a wrong zone file.
                     * @todo 20160106 edf -- : factorize with "nsec3_add_label" (if possible ?)
                     */

                    /*
                        * The node existed already but has now been changed.
                        */

                    /*
                        * DYNUPDATE:
                        *
                        * The node will change.
                        *
                        * If the node is not marked
                        *   Mark the node for future add and output it now
                        *
                        */

                    if((node->flags & NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD) == 0)
                    {
                        zdb_listener_notify_remove_nsec3(zone, node, n3, min_ttl);
                        node->flags |= NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD;
                    }

                    type_bit_maps_size = type_context->type_bit_maps_size;

                    /* LOCK */
                    ZFREE_ARRAY(node->type_bit_maps, node->type_bit_maps_size);
                    /* UNLOCK */

                    if(type_bit_maps_size > 0)
                    {
                        /* LOCK */
                        ZALLOC_ARRAY_OR_DIE(u8*, node->type_bit_maps, type_bit_maps_size, NSEC3_TYPEBITMAPS_TAG);
                        /* UNLOCK */

                        node->type_bit_maps_size = type_bit_maps_size;

                        type_bit_maps_write(node->type_bit_maps, type_context);
                    }
                    /*
                     * This case does not exist:  A merge of something of size > 0
                     * with anything will always give a size > 0
                     *
                     * else
                     * {
                     *   node->type_bit_maps_size = 0;
                     * }
                     *
                     */
                }

                /* LOCK */
                ZFREE_ARRAY(tmp_type_bit_maps, type_bit_maps_size);
                /* UNLOCK */
            }

            /* nsec3_set_label_extension */

            yassert(node != NULL);

            n3ext->self = node;
            n3ext = n3ext->next;

        }
#if NSEC3_UPDATE_ZONE_DEBUG!=0
        else
        {
            MEMCOPY(&digest[1], &n3ext->self->digest[1], n3ext->self->digest[0]);

            log_debug("nsec3: done '%{dnsname}' %{digest32h} ", name, digest);
        }
#endif

        n3 = n3->next;
    }
    while(n3 != NULL);
    
#if NSEC3_UPDATE_ZONE_DEBUG
    log_debug("nsec3_update_label_nsec3_nodes_recursive(%3d, %{dnsname}) : NSEC3", label_stack_level, debug_name);
#endif
    
    return TRUE;
}

static void
nsec3_update_zone_nsec3_nodes_recursive(zdb_zone *zone, bool opt_out)
{
    nsec3_update_zone_nsec3_nodes_recursive_args commonargs;
      
    ZEROMEMORY(&commonargs, sizeof(commonargs));
    
    commonargs.zone = zone;
    commonargs.label_stack[0] = zone->apex;
    commonargs.label_stack_level = 0;
    commonargs.origin_len = dnsname_len(zone->origin);
    zdb_zone_getminttl(zone, &commonargs.min_ttl);
    commonargs.opt_out = opt_out;
    commonargs.nsec3_flags = (opt_out)?1:0;
    
    nsec3_update_label_nsec3_nodes_recursive(&commonargs);
    
    log_debug("nsec3: parsed %u labels, seen %u delegations, made %u NSEC3 records",
              commonargs.internal_statistics_label_count,
              commonargs.internal_statistics_delegation_count,
              commonargs.internal_statistics_nsec3_count);
}

/**
 * Updates ALL the NSEC3 records for ALL the labels, and this for ALL the NSEC3PARAM of the zone.
 * After this call, a signature update must be called/scheduled on the zone.
 */

ya_result
nsec3_update_zone(zdb_zone* zone)
{
    /**
     * If it is NSEC: prepare to remove all NSEC information
     * If it is NSEC3: just do a normal update
     *
     * For now, I'm assuming that there is nothing yet.
     *
     */

    if(zone->nsec.nsec3 == NULL)
    {
        return SUCCESS; /* Nothing to do */
    }

    if((zone->apex->flags & ZDB_RR_LABEL_NSEC) != 0)
    {
        return DNSSEC_ERROR_NSEC3_INVALIDZONESTATE; /* NSEC3 update of an NSEC zone is not supported */
    }

    bool opt_out = zdb_zone_is_nsec3_optout(zone);
        
    u32 min_ttl = 900;
    
    zdb_zone_getminttl(zone, &min_ttl);

    u8 name[2 + MAX_DOMAIN_LENGTH];

#if NSEC3_UPDATE_ZONE_DEBUG!=0
    log_debug("nsec3: zone '%{dnsname}'", zone->origin);
#endif

    /*
     * All the labels from (included) the root to the zone have to be added
     * These ones must be handled differently than the ones in the zone.
     */

    u8 digest[1 + MAX_DIGEST_LENGTH];
    
#if NSEC3_INCLUDE_ZONE_PATH
    
    /**
     * This now disabled code was used so YADIFA was compatible with the
     * named 9.6.? NSEC3 chain.
     * 
     * It is now obsolete as 9.6.? does not do that anymore.
     */

    u8* zone_path = zone->origin;
    zone_path += (*zone_path) + 1;

    for(;;)
    {
        /*
         * There are NO types here
         */

#if NSEC3_UPDATE_ZONE_DEBUG!=0
        log_debug("nsec3: path '%{dnsname}'", zone_path);
#endif

        nsec3_zone* n3 = zone->nsec.nsec3;

        do
        {
            nsec3_compute_digest_from_fqdn_with_len(n3, zone_path, dnsname_len(zone_path), digest, FALSE);

            //log_debug("nsec3_update_zone: creating node: %{digest32h} NSEC3 ; %{dnsname} (zone)", digest, zone_path);

            nsec3_zone_item* node = nsec3_avl_insert(&n3->items, digest);

            node->flags = (opt_out)?1:0;
            node->flags |= NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD;

            if(!nsec3_is_owned_by(node, NSEC3_ZONE_FAKE_OWNER))
            {
                nsec3_add_owner(node, NSEC3_ZONE_FAKE_OWNER); /* Zone proprietary */
            }

            n3 = n3->next;
        }
        while(n3 != NULL);

        if(*zone_path == 0)
        {
            break;
        }

        zone_path += (*zone_path) + 1;
    }
#else
    // generate a normal NSEC3 chain
#endif
    
    zdb_zone_label_iterator label_iterator;
  
    nsec3_update_zone_nsec3_nodes_recursive(zone, opt_out);

    /**
     * NSEC3 nodes have been removed (ixfr) as soon as it was required
     * Now all the added nodes (edited(remove+add) nodes and new nodes
     * are marked with NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD.
     *
     * This flag MUST be removed (it's not a valid flag) and the node
     * must be send as added for ixfr.
     *
     * NOTE: When I speak about IXFR I speak about the mechanism, not
     * the network transfer.
     *
     * NOTE: This should be improved for the TLD:
     *
     *       When doing a small update (and not the first init) I should
     *       use a list of nodes that have their flag set. (ptr_vector)
     *
     *       But on the first pass (initialization) this is the best way.
     */

    {
        nsec3_zone* n3 = zone->nsec.nsec3;

        do
        {
            nsec3_avl_iterator iter;
            nsec3_avl_iterator_init(&n3->items, &iter);

            while(nsec3_avl_iterator_hasnext(&iter))
            {
                nsec3_zone_item* node = nsec3_avl_iterator_next_node(&iter);

                if((node->flags & NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD) != 0)
                {
                    node->flags &= ~NSEC3_FLAGS_MARKED_FOR_ICMTL_ADD;
                    zdb_listener_notify_add_nsec3(zone, node, n3, min_ttl);
                }
            }

            n3 = n3->next;
        }
        while(n3 != NULL);
    }

    /*
     * In order to avoid computing the *.fqdn digest when needed, we do it here and store it for later
     */

    name[0] = 1;
    name[1] = '*';
    
    //zdb_zone_label_iterator label_iterator;

    zdb_zone_label_iterator_init(&label_iterator, zone);

    while(zdb_zone_label_iterator_hasnext(&label_iterator))
    {
        u32 name_len = zdb_zone_label_iterator_nextname(&label_iterator, &name[2]) + 2;

#if NSEC3_UPDATE_ZONE_DEBUG!=0
        log_debug("nsec3: wild '%{dnsname}'", name);
#endif

        zdb_rr_label* label = zdb_zone_label_iterator_next(&label_iterator);

        if(label->nsec.nsec3 == NULL || label->nsec.nsec3->star != NULL)
        {
            /*
             * Already done.
             */

#if NSEC3_UPDATE_ZONE_DEBUG!=0
            log_debug("nsec3: wild '%{dnsname}' already set", name);
#endif
            continue;
        }

        nsec3_zone* n3 = zone->nsec.nsec3;

        nsec3_label_extension* n3ext = label->nsec.nsec3;

        do
        {
            yassert(n3ext != NULL);

            /* Compute the digest */

            nsec3_compute_digest_from_fqdn_with_len(n3, name, name_len, digest, FALSE);

            //log_debug("nsec3_update_zone: \"precalc\" node: %{digest32h} NSEC3 ; %{dnsname}", digest, name);

#if NSEC3_UPDATE_ZONE_DEBUG!=0
            log_debug("nsec3: wild '%{dnsname}' %{digest32h} ", name, digest);
#endif
            nsec3_zone_item* node = nsec3_avl_find_interval_start(&n3->items, digest);

#if NSEC3_UPDATE_ZONE_DEBUG!=0
            log_debug("nsec3: *. => %{digest32h} ", node->digest);
#endif

            nsec3_add_star(node, label);

            yassert(n3ext->star == NULL);

            n3ext->star = node;
            n3ext = n3ext->next;

            n3 = n3->next;
        }
        while(n3 != NULL);

        yassert(n3ext == NULL);
    }

    return SUCCESS;
}

/// @note 20150915 edf -- the first NSEC3PARAM is the one used for queries.  The other NSEC3PARAM chain must
///                       be complete before removing this one.
///                       And this tells the way to do this properly.
///                       When an NSEC3PARAM is added, all the required processing should be computed in the
///                       background.  The NSEC3PARAM has to be added only at the end.
///                       When an NSEC3PARAM is removed (can be) it should only be removed at the end.
///                       All the changes must be written in the journal and should be visible in the DB.
///                       Deducing than an NSEC3PARAM is missing should be deduced from the NSEC3 without a chain.
///                       I may have to prevent YADIFA to accept an update when one is already on the way.
///                       This limitation may be removed later.
/// @note 20150916 edf -- the zone needs to be marked as generating an NSEC3 chain.
///                       the zone needs to be marked as deleting an NSEC3 chain (which one)
///                       the program may be stopped while doing it so I need a way to tell what I'm doing
///                       in the journal.



void
nsec3_update_zone_new_nsec3param(zdb_zone* zone, zdb_packed_ttlrdata* new_nsec3param)
{
    zdb_icmtl icmtl;
    ya_result ret;

    // start to write to the journal (the update triggering this should have stopped)
    // add an experimental "started to add an nsec3param"
    // stop to write to the journal
    
    if(ISOK(ret = zdb_icmtl_begin(&icmtl, zone)))
    {
        if(zdb_record_insert_checked(&zone->apex->resource_record_set, TYPE_NSEC3PARAMADD, new_nsec3param))
        {
        }
        
        if(FAIL(ret = zdb_icmtl_end(&icmtl)))
        {
            log_err("update: new nsec3param: failed to close the journal: %r", ret);
            return;
        }
    }
    else
    {
        log_err("update: new nsec3param: failed to open the journal: %r", ret);
        return;
    }
    
    // loop:
    //  start to write to the journal (the update triggering this should have stopped)
    //  compute a batch of nsec3 updates (don't sign them as they are not supposed to be used yet)
    //  write the updates
    //  stop to write to the journal
    
    // nsec3_forall_label(zdb_zone *zone, s8 chain_index, bool callback_need_fqdn, bool opt_out, nsec3_forall_label_callback *callback, void *callback_args)
    
    for(;;)
    {
        if(ISOK(ret = zdb_icmtl_begin(&icmtl, zone))) // @todo 20160106 edf -- loop this until doomsday
        {
            if(zdb_record_insert_checked(&zone->apex->resource_record_set, TYPE_NSEC3PARAMADD, new_nsec3param))
            {
            }
            
            if(FAIL(ret = zdb_icmtl_end(&icmtl)))
            {
                log_err("update: new nsec3param: failed to close the journal: %r", ret);
                return;
            }
        }
        else
        {
            log_err("update: new nsec3param: failed to open the journal: %r", ret);
            return;
        }
    }
    
    // start to write to the journal (the update triggering this should have stopped)
    // add the nsec3param
    // update the signatures of the nsec3param & nsec3 of this chain
    // stop to write to the journal
    
    // start to write to the journal (the update triggering this should have stopped)
    // remove an experimental "started to add an nsec3param
    // stop to write to the journal
  
    // if there is a task to remove an nsec3param, start it now
    
}

void
nsec3_update_zone_remove_nsec3param(zdb_zone* zone, zdb_packed_ttlrdata* new_nsec3param)
{
    // start to write to the journal (the update triggering this should have stopped)
    //  remove the nsec3param
    
    // I'll use 0xFF00 for the type (Reserved for private use)
    
    // loop:
    //  start to write to the journal (the update triggering this should have stopped)
    //  remove a batch of nsec3 items linked to the nsec3param
    //  write the updates
    //  stop to write to the journal
    
    // start to write to the journal (the update triggering this should have stopped)
    // add the nsec3param
    // update the signatures of the nsec3param & nsec3 of this chain
    // stop to write to the journal
    
    // start to write to the journal (the update triggering this should have stopped)
    // remove an experimental "started to delete an nsec3param
    // stop to write to the journal
    
    // if there is a task to remove an nsec3param, start it now
}

/** @} */

/*----------------------------------------------------------------------------*/

