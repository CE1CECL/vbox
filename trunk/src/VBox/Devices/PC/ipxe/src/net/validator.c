/*
 * Copyright (C) 2012 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * Oracle GPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the General Public License version 2 (GPLv2) at this time for any software where
 * a choice of GPL license versions is made available with the language indicating
 * that GPLv2 or any later version may be used, or where a choice of which version
 * of the GPL is applied is otherwise unspecified.
 */

FILE_LICENCE ( GPL2_OR_LATER );

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ipxe/refcnt.h>
#include <ipxe/malloc.h>
#include <ipxe/interface.h>
#include <ipxe/xfer.h>
#include <ipxe/open.h>
#include <ipxe/iobuf.h>
#include <ipxe/xferbuf.h>
#include <ipxe/process.h>
#include <ipxe/x509.h>
#include <ipxe/settings.h>
#include <ipxe/dhcp.h>
#include <ipxe/base64.h>
#include <ipxe/crc32.h>
#include <ipxe/validator.h>

/** @file
 *
 * Certificate validator
 *
 */

/** A certificate validator */
struct validator {
	/** Reference count */
	struct refcnt refcnt;
	/** Job control interface */
	struct interface job;
	/** Data transfer interface */
	struct interface xfer;
	/** Process */
	struct process process;
	/** X.509 certificate chain */
	struct x509_chain *chain;
	/** Data buffer */
	struct xfer_buffer buffer;
};

/**
 * Free certificate validator
 *
 * @v refcnt		Reference count
 */
static void validator_free ( struct refcnt *refcnt ) {
	struct validator *validator =
		container_of ( refcnt, struct validator, refcnt );

	DBGC2 ( validator, "VALIDATOR %p freed\n", validator );
	x509_chain_put ( validator->chain );
	xferbuf_done ( &validator->buffer );
	free ( validator );
}

/**
 * Mark certificate validation as finished
 *
 * @v validator		Certificate validator
 * @v rc		Reason for finishing
 */
static void validator_finished ( struct validator *validator, int rc ) {

	/* Remove process */
	process_del ( &validator->process );

	/* Close all interfaces */
	intf_shutdown ( &validator->xfer, rc );
	intf_shutdown ( &validator->job, rc );
}

/****************************************************************************
 *
 * Job control interface
 *
 */

/** Certificate validator job control interface operations */
static struct interface_operation validator_job_operations[] = {
	INTF_OP ( intf_close, struct validator *, validator_finished ),
};

/** Certificate validator job control interface descriptor */
static struct interface_descriptor validator_job_desc =
	INTF_DESC ( struct validator, job, validator_job_operations );

/****************************************************************************
 *
 * Cross-signing certificates
 *
 */

/** Cross-signed certificate source setting */
struct setting crosscert_setting __setting ( SETTING_CRYPTO ) = {
	.name = "crosscert",
	.description = "Cross-signed certificate source",
	.tag = DHCP_EB_CROSS_CERT,
	.type = &setting_type_string,
};

/** Default cross-signed certificate source */
static const char crosscert_default[] = "http://ca.ipxe.org/auto";

/**
 * Start download of cross-signing certificate
 *
 * @v validator		Certificate validator
 * @v issuer		Required issuer
 * @ret rc		Return status code
 */
static int validator_start_download ( struct validator *validator,
				      const struct asn1_cursor *issuer ) {
	const char *crosscert;
	char *crosscert_copy;
	char *uri_string;
	size_t uri_string_len;
	uint32_t crc;
	int len;
	int rc;

	/* Determine cross-signed certificate source */
	len = fetch_string_setting_copy ( NULL, &crosscert_setting,
					  &crosscert_copy );
	if ( len < 0 ) {
		rc = len;
		DBGC ( validator, "VALIDATOR %p could not fetch crosscert "
		       "setting: %s\n", validator, strerror ( rc ) );
		goto err_fetch_crosscert;
	}
	crosscert = ( crosscert_copy ? crosscert_copy : crosscert_default );

	/* Allocate URI string */
	uri_string_len = ( strlen ( crosscert ) + 22 /* "/%08x.der?subject=" */
			   + base64_encoded_len ( issuer->len ) + 1 /* NUL */ );
	uri_string = zalloc ( uri_string_len );
	if ( ! uri_string ) {
		rc = -ENOMEM;
		goto err_alloc_uri_string;
	}

	/* Generate CRC32 */
	crc = crc32_le ( 0xffffffffUL, issuer->data, issuer->len );

	/* Generate URI string */
	len = snprintf ( uri_string, uri_string_len, "%s/%08x.der?subject=",
			 crosscert, crc );
	base64_encode ( issuer->data, issuer->len, ( uri_string + len ) );
	DBGC ( validator, "VALIDATOR %p downloading cross-signed certificate "
	       "from %s\n", validator, uri_string );

	/* Open URI */
	if ( ( rc = xfer_open_uri_string ( &validator->xfer,
					   uri_string ) ) != 0 ) {
		DBGC ( validator, "VALIDATOR %p could not open %s: %s\n",
		       validator, uri_string, strerror ( rc ) );
		goto err_open_uri_string;
	}

	/* Success */
	rc = 0;

 err_open_uri_string:
	free ( uri_string );
 err_alloc_uri_string:
	free ( crosscert_copy );
 err_fetch_crosscert:
	return rc;
}

/**
 * Append cross-signing certificates to certificate chain
 *
 * @v validator		Certificate validator
 * @v data		Raw cross-signing certificate data
 * @v len		Length of raw data
 * @ret rc		Return status code
 */
static int validator_append ( struct validator *validator,
			      const void *data, size_t len ) {
	struct asn1_cursor cursor;
	struct x509_chain *certs;
	struct x509_certificate *cert;
	struct x509_certificate *last;
	int rc;

	/* Allocate certificate list */
	certs = x509_alloc_chain();
	if ( ! certs ) {
		rc = -ENOMEM;
		goto err_alloc_certs;
	}

	/* Initialise cursor */
	cursor.data = data;
	cursor.len = len;

	/* Enter certificateSet */
	if ( ( rc = asn1_enter ( &cursor, ASN1_SET ) ) != 0 ) {
		DBGC ( validator, "VALIDATOR %p could not enter "
		       "certificateSet: %s\n", validator, strerror ( rc ) );
		goto err_certificateset;
	}

	/* Add each certificate to list */
	while ( cursor.len ) {

		/* Add certificate to chain */
		if ( ( rc = x509_append_raw ( certs, cursor.data,
					      cursor.len ) ) != 0 ) {
			DBGC ( validator, "VALIDATOR %p could not append "
			       "certificate: %s\n",
			       validator, strerror ( rc) );
			DBGC_HDA ( validator, 0, cursor.data, cursor.len );
			return rc;
		}
		cert = x509_last ( certs );
		DBGC ( validator, "VALIDATOR %p found certificate %s\n",
		       validator, cert->subject.name );

		/* Move to next certificate */
		asn1_skip_any ( &cursor );
	}

	/* Append certificates to chain */
	last = x509_last ( validator->chain );
	if ( ( rc = x509_auto_append ( validator->chain, certs ) ) != 0 ) {
		DBGC ( validator, "VALIDATOR %p could not append "
		       "certificates: %s\n", validator, strerror ( rc ) );
		goto err_auto_append;
	}

	/* Check that at least one certificate has been added */
	if ( last == x509_last ( validator->chain ) ) {
		DBGC ( validator, "VALIDATOR %p failed to append any "
		       "applicable certificates\n", validator );
		rc = -EACCES;
		goto err_no_progress;
	}

	/* Drop reference to certificate list */
	x509_chain_put ( certs );

	return 0;

 err_no_progress:
 err_auto_append:
 err_certificateset:
	x509_chain_put ( certs );
 err_alloc_certs:
	return rc;
}

/****************************************************************************
 *
 * Data transfer interface
 *
 */

/**
 * Close data transfer interface
 *
 * @v validator		Certificate validator
 * @v rc		Reason for close
 */
static void validator_xfer_close ( struct validator *validator, int rc ) {

	/* Close data transfer interface */
	intf_restart ( &validator->xfer, rc );

	/* Check for errors */
	if ( rc != 0 ) {
		DBGC ( validator, "VALIDATOR %p download failed: %s\n",
		       validator, strerror ( rc ) );
		goto err_download;
	}
	DBGC2 ( validator, "VALIDATOR %p download complete\n", validator );

	/* Append downloaded certificates */
	if ( ( rc = validator_append ( validator, validator->buffer.data,
				       validator->buffer.len ) ) != 0 )
		goto err_append;

	/* Free downloaded data */
	xferbuf_done ( &validator->buffer );

	/* Resume validation process */
	process_add ( &validator->process );

	return;

 err_append:
 err_download:
	validator_finished ( validator, rc );
}

/**
 * Receive data
 *
 * @v validator		Certificate validator
 * @v iobuf		I/O buffer
 * @v meta		Data transfer metadata
 * @ret rc		Return status code
 */
static int validator_xfer_deliver ( struct validator *validator,
				    struct io_buffer *iobuf,
				    struct xfer_metadata *meta ) {
	int rc;

	/* Add data to buffer */
	if ( ( rc = xferbuf_deliver ( &validator->buffer, iob_disown ( iobuf ),
				      meta ) ) != 0 ) {
		DBGC ( validator, "VALIDATOR %p could not receive data: %s\n",
		       validator, strerror ( rc ) );
		validator_finished ( validator, rc );
		return rc;
	}

	return 0;
}

/** Certificate validator data transfer interface operations */
static struct interface_operation validator_xfer_operations[] = {
	INTF_OP ( xfer_deliver, struct validator *, validator_xfer_deliver ),
	INTF_OP ( intf_close, struct validator *, validator_xfer_close ),
};

/** Certificate validator data transfer interface descriptor */
static struct interface_descriptor validator_xfer_desc =
	INTF_DESC ( struct validator, xfer, validator_xfer_operations );

/****************************************************************************
 *
 * Validation process
 *
 */

/**
 * Certificate validation process
 *
 * @v validator		Certificate validator
 */
static void validator_step ( struct validator *validator ) {
	struct x509_certificate *last = x509_last ( validator->chain );
	time_t now;
	int rc;

	/* Try validating chain.  Try even if the chain is incomplete,
	 * since certificates may already have been validated
	 * previously.
	 */
	now = time ( NULL );
	if ( ( rc = x509_validate_chain ( validator->chain, now,
					  NULL ) ) == 0 ) {
		validator_finished ( validator, 0 );
		return;
	}

	/* If chain ends with a self-issued certificate, then there is
	 * nothing more to do.
	 */
	if ( asn1_compare ( &last->issuer.raw, &last->subject.raw ) == 0 ) {
		validator_finished ( validator, rc );
		return;
	}

	/* Otherwise, try to download a suitable cross-signing
	 * certificate.
	 */
	if ( ( rc = validator_start_download ( validator,
					       &last->issuer.raw ) ) != 0 ) {
		validator_finished ( validator, rc );
		return;
	}
}

/** Certificate validator process descriptor */
static struct process_descriptor validator_process_desc =
	PROC_DESC_ONCE ( struct validator, process, validator_step );

/****************************************************************************
 *
 * Instantiator
 *
 */

/**
 * Instantiate a certificate validator
 *
 * @v job		Job control interface
 * @v chain		X.509 certificate chain
 * @ret rc		Return status code
 */
int create_validator ( struct interface *job, struct x509_chain *chain ) {
	struct validator *validator;
	int rc;

	/* Sanity check */
	if ( ! chain ) {
		rc = -EINVAL;
		goto err_sanity;
	}

	/* Allocate and initialise structure */
	validator = zalloc ( sizeof ( *validator ) );
	if ( ! validator ) {
		rc = -ENOMEM;
		goto err_alloc;
	}
	ref_init ( &validator->refcnt, validator_free );
	intf_init ( &validator->job, &validator_job_desc,
		    &validator->refcnt );
	intf_init ( &validator->xfer, &validator_xfer_desc,
		    &validator->refcnt );
	process_init ( &validator->process, &validator_process_desc,
		       &validator->refcnt );
	validator->chain = x509_chain_get ( chain );

	/* Attach parent interface, mortalise self, and return */
	intf_plug_plug ( &validator->job, job );
	ref_put ( &validator->refcnt );
	DBGC2 ( validator, "VALIDATOR %p validating X509 chain %p\n",
		validator, validator->chain );
	return 0;

	validator_finished ( validator, rc );
	ref_put ( &validator->refcnt );
 err_alloc:
 err_sanity:
	return rc;
}
