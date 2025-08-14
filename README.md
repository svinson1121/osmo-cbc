osmo-cbc - Osmocom Cell Broadcast Centre
========================================

This repository contains a C-language implementation of a minimal
3GPP Cell Broadcast Centre (CBC). It is part of the
[Osmocom](https://osmocom.org/) Open Source Mobile Communications
project.

A Cell Broadcast Centre is the central network element of a cellular network
for distribution of Cell Broadcast and Emergency messages.

This code implements
 * the CBSP protocol on the CBC-BSC interface
 * a custom HTTP/REST based interface for external users to create/delete CBS messages

We plan to add support for the following features in the future:
 * the SABP protocol on the CBC-RNC (or CBC-HNBGW) interface for UMTS support
 * the SBcAP protocol on the CBC-MME interface for LTE support

Homepage
--------

The official homepage of the project is
https://osmocom.org/projects/osmo-cbc/wiki

GIT Repository
--------------

You can clone from the official osmo-cbc.git repository using

	git clone https://gitea.osmocom.org/cellular-infrastructure/osmo-cbc

There is a web interface at <https://gitea.osmocom.org/cellular-infrastructure/osmo-cbc>

Documentation
-------------

User Manuals and VTY reference manuals are [optionally] built in PDF form
as part of the build process.

Pre-rendered PDF version of the current "master" can be found at
[User Manual](https://ftp.osmocom.org/docs/latest/osmocbc-usermanual.pdf)
as well as the [VTY Reference Manual for osmo-cbc](https://ftp.osmocom.org/docs/latest/osmocbc-vty-reference.pdf)

Mailing List
------------

Discussions related to osmo-cbc are happening on the
openbsc@lists.osmocom.org mailing list, please see
https://lists.osmocom.org/mailman/listinfo/openbsc for subscription
options and the list archive.

Please observe the [Osmocom Mailing List
Rules](https://osmocom.org/projects/cellular-infrastructure/wiki/Mailing_List_Rules)
when posting.

Contributing
------------

Our coding standards are described at
https://osmocom.org/projects/cellular-infrastructure/wiki/Coding_standards

We use a Gerrit based patch submission/review process for managing
contributions.  Please see
https://osmocom.org/projects/cellular-infrastructure/wiki/Gerrit for
more details

The current patch queue for osmo-cbc can be seen at
https://gerrit.osmocom.org/#/q/project:osmo-cbc+status:open


Generating asn1c code
---------------------

Upstream master as1nc from [vlm](https://github.com/vlm/asn1c) [doesn't support
APER encoding](https://github.com/vlm/asn1c/issues/452). Nevertheless, the
upstream fork maintained by a big contributor
[mouse07410](https://github.com/mouse07410/asn1c) does support it, and it is
used in osmo-cbc to generate the SBc-AP code from ASN.1 files present in
src/sbcap/asn1/.

In order to regenerate the code, one shall adjust the ASN1C_SKELETON_PATH and
ASN1C_BIN_PATH in configure.ac to point to the built & installed asn1c from
mouse07410 (usually `vlm_master` branch). Last generated code was built using
commit hash 08b293e8aa342d465d26805d1d66f3595b2ce261.

Then, do the usual `autoreconf -fi && ./configure`, using a buildir != srcdir
(important, in order to avoid ending up with tempotary files in srcdir and
making it difficult to stash the relevant changes).

Finally, run `make -C src/ regen`, which will regenerate the files and copy over
the skeletons, with git possibily showing changes in the following paths:
- include/osmocom/sbcap/
- src/sbcap/gen/
- src/sbcap/skel/


- Updates:
* added CORS support to the rest API
* fixed typo "tsuname" -> tsunami in rest api 
* Chanhge output of Msgid and SerNo of "show messages cbc/etws" from  HEX to DEC

CBC Updates - 2025-08-14

- Add EXPIRED FSM state: messages automatically expire after warning_period_sec.
- Remove warning_period_sec from REST API; calculate from Repetition Period and Number of Broadcasts (rep_period x num_bcast + 30s padding).
- Update VTY command `delete message expired id {MsgId}` to delete all expired messages sharing the same Message ID; active messages remain unaffected.
- Convert rep_period units to seconds per 3GPP spec (1.883s/unit) and add logging for calculated warning_period_sec.

- Todo:
  * Add support in the API for PLMN and TAC in scope, so we can send alerts to a group of TACs or a specific PLMN

