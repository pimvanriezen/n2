#include "datatypes.h"
#include "n2diskdb.h"
#include "n2encoding.h"
#include "iptypes.h"
#include "n2acl.h"
#include "n2config.h"
#include "n2malloc.h"
#include "hcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

typedef struct ipnode_st
{
	hstat st;
	struct ipnode_st *next;
} ipnode;

/* ------------------------------------------------------------------------- *\
 * FUNCTION main (argc, argv)                                                *
 * --------------------------                                                *
 * Goes over configured host-groups and fishes for hosts in /var/state/n2    *
 * for some summary information. Prints out the information either in a CSV  *
 * format (default), in XML (with -x) or as tabulated human-readable data    *
 * (with -f). An optional groupname argument will limit the scope of the     *
 * search to a single host-group.                                            *
\* ------------------------------------------------------------------------- */
int main (int argc, char *argv[])
{
	DIR *dir;
	struct dirent *de;
	FILE *fsumm;
	char *name;
	unsigned int addr;
	hostgroup *grp;
	int first=1;
	netload_rec *rec;
	hstat *info;
	unsigned long long netin, netout;
	int numwarn, numalert, numcrit;
	int rtt, count;
	int asxml;
	int ascsv;
	int asjson=0;
	int op;
	int firstmember=0;
	int firstgroup=1;
	char outline[256];
	char addrbuf[32];
	const char *groupname = NULL;
	ipnode *firstnode = NULL;
	ipnode *currentnode = NULL;
	ipnode *newnode = NULL;
	int isacked;
	
	asxml = 0;
	ascsv = 1;
	
	while ((op = getopt (argc, argv, "xfjg:")) > 0)
	{
		switch (op)
		{
			case 'x': asjson=0; asxml=1; ascsv=0; break;
			case 'f': asjson=0; ascsv=0; asxml=0; break;
			case 'j': asxml=0; ascsv=0; asjson=1; break;
			case 'g': groupname=optarg; break;
		}
	}
	
	conf_init ();
	acl_init ();
	load_config ("/etc/n2/n2rxd.conf");
	
	if (asxml)
	{
		printf ("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
		printf ("<nl.madscience.svc.n2.groups>\n");
	}
	else if ((!ascsv)&&(!asjson))
	{
		printf ("Host              Status   Load      CPU    RTT   "
				"Loss  Net In/    Out     i/o\n");
				//                    Kb/s   "
	}
	else if (asjson)
	{
		printf ("{");
	}
	
	fsumm = fopen ("/var/state/n2/current/summary", "r");
	if (! fsumm)
	{
		fprintf (stderr, "%% Error loading summary file\n");
		return 1;
	}

	newnode = (ipnode *) malloc (sizeof (ipnode));

	while (fread (newnode, sizeof (hstat), 1, fsumm) > 0)
	{
		newnode->next = NULL;
		if (currentnode)
		{
			currentnode->next = newnode;
			currentnode = newnode;
		}
		else
		{
			currentnode = firstnode = newnode;
		}

		newnode = (ipnode *) malloc (sizeof (ipnode));
	}
	fclose (fsumm);
	free (newnode);

	grp = GROUPS.groups;
	
	while (grp)
	{
		if ((! groupname) || (! strcmp (groupname, grp->name)))
		{
			netin = netout = 0;
			numwarn = numalert = numcrit = rtt = count = 0;
			if (ascsv)
			{
				if (ascsv) printf ("%s:", grp->name);
			}
			else if (asjson)
			{
				if (firstgroup) firstgroup=0;
				else printf (",");
				printf ("\"%s\":{\"description\":\"%s\",\"members\":{",
						grp->name, grp->description);
				firstmember=1;
			}
			else if (asxml)
			{
				printf ("  <group name=\"%s\" description=\"%s\">\n",
											grp->name, grp->description);
				printf ("    <members>\n");
			}
			first = 1;
			currentnode = firstnode;
			while (currentnode)
			{
				addr = currentnode->st.addr;
				printip (addr, addrbuf);
				if (grp == hostgroup_acl_resolve (addr))
				{
					isacked = 0;
					if (asxml)
					{
						printf ("      <member ip=\"%s\"", addrbuf);
					}
					else if (asjson)
					{
						if (firstmember) firstmember=0;
						else printf (",");
						printf ("\"%s\":{", addrbuf);
					}
					else if (ascsv)
					{
						printf ("%s%s", first ? "" : " ", addrbuf);
					}
					first = 0;
					info = &(currentnode->st);
					if (info)
					{
						if (CHKOFLAG(info->oflags,OFLAG_ACKED))
						{
							// hide the flag, but keep it tracked.
							info->oflags ^= 1<<OFLAG_ACKED;
							isacked = 1;
						}
						else isacked = 0;
						if (asxml || asjson)
						{
							// FIXME@ koert: potential stack overflow here if error flags are added
							// or get longer names
							char flags[512];
							flags[0] = '\0';

							if( CHKSTATUSFLAG(info->status,FLAG_RTT) ) strcat(flags,", rtt");
							if( CHKSTATUSFLAG(info->status,FLAG_LOSS) ) strcat(flags,", loss");
							if( CHKSTATUSFLAG(info->status,FLAG_LOAD) ) strcat(flags,", load");
							if( CHKOFLAG(info->oflags,OFLAG_RAM) ) strcat(flags,", ram");
							if( CHKOFLAG(info->oflags,OFLAG_SWAP) ) strcat(flags,", swap");
							if( CHKOFLAG(info->oflags,OFLAG_NETIN) ) strcat(flags,", netin");
							if( CHKOFLAG(info->oflags,OFLAG_NETOUT) ) strcat(flags,", netout");
							if( CHKOFLAG(info->oflags,OFLAG_SVCDOWN) ) strcat(flags,", svcdown");
							if( CHKOFLAG(info->oflags,OFLAG_DISKIO) ) strcat(flags,", diskio");
							if( CHKOFLAG(info->oflags,OFLAG_DISKSPACE) ) strcat(flags,", diskspace");
							if( CHKOFLAG(info->oflags,OFLAG_DECODINGERR) ) strcat(flags,", decodingerr");
							if( CHKOFLAG(info->oflags,OFLAG_IOWAIT) ) strcat(flags,", iowait");
							// if( CHKSTATUSFLAG(info->status,FLAG_OTHER) ) strcat(flags,", other");

							if (asxml)
							{
								printf (" netin=\"%u\" netout=\"%u\" "
										"rtt=\"%.1f\" cpu=\"%.2f\" "
										"loadavg=\"%.2f\" status=\"%s\" "
										"diskio=\"%u\" flags=\"%s\" ",
										info->netin, info->netout,
										((double) info->ping10) / 10.0,
										((double) info->cpu) / 2.56,
										((double) info->load1) / 100.0,
										isacked ? "ACKED" : STR_STATUS[info->status & 15],
										info->diskio,
										*flags ? flags+2 : flags );
							}
							else
							{
								printf ("\"netin\":%u,", info->netin);
								printf ("\"netout\":%u,", info->netout);
								printf ("\"rtt\":%.1f,", ((double)info->ping10)/10.0);
								printf ("\"cpu\":%.2f,",((double) info->cpu)/2.56);
								printf ("\"loadavg\":%.2f,",((double)info->load1)/100.0);
								printf ("\"status\":\"%s\",",isacked?"ACKED":STR_STATUS[info->status&15]);
								printf ("\"diskio\":%u,",info->diskio);
								printf ("\"flags\":\"%s\"", *flags?flags+2:flags);
							}
						}
						else if (! ascsv)
						{
							sprintf (outline, "%-17s                  ",
									 addrbuf);
							sprintf (outline+18, "%s        ",
									 isacked ? "ACKED" : STR_STATUS[info->status&15]);
							if (info->status == ST_DEAD)
							{
								sprintf (outline+24, "   -.--   -.-- %% "
										 "%6.1f  %3i %%       -/      -"
										 "       -",
										 ((double)info->ping10) / 10.0,
										 info->loss/100
										);
							}
							else sprintf (outline+24, " %6.2f %6.2f %% "
									 "%6.1f  %3i %% %7i/%7i %7i",
									 ((double) info->load1) / 100.0,
									 ((double) info->cpu) / 2.56,
									 ((double) info->ping10) / 10.0,
									 info->loss/100,
									 info->netin, info->netout,
									 info->diskio);
									 
							printf ("%s\n", outline); 
						}
						netin += info->netin;
						netout += info->netout;
						rtt += info->ping10;
						if (ascsv)
						{
							switch (RDSTATUS(info->status))
							{
								case ST_WARNING:
									if (isacked) break;
									printf ("=WARNING");
									++numwarn; break;

								case ST_STALE:
								case ST_ALERT:
								case ST_DEAD:
									if (isacked) break;
									printf ("=ALERT");
									++numalert; break;

								case ST_CRITICAL:
									if (isacked) break;
									printf ("=CRITICAL");
									++numcrit; break;
								
								default:
									printf ("=OK");
									break;
							}
						}
						pool_free (info);
					}
					else
					{
						free (rec);
					}
					if (asxml) printf ("/>\n");
					if (asjson) printf ("}");
					++count;
				}
				currentnode = currentnode->next;
			}
			if (asjson)
			{
				printf ("},\"summary\":{");
				printf ("\"netin\":%u,", netin);
				printf ("\"netout\":%u,",netout);
				printf ("\"rtt\":%.1f,",((double)rtt/(10.0*((double)count))));
				printf ("\"counts.warning\":%i,", numwarn);
				printf ("\"counts.alert\":%i,", numalert);
				printf ("\"counts.critical\":%i", numcrit);
				printf ("}}");
			}
			else if (! asxml)
			{
				if (! count) count=1;
				if (ascsv)
				{
					printf (":%i:%llu:%llu:%.1f:%i:%i:%i\n",
							count, netin, netout,
							((double) rtt / (10.0 * ((double) count))),
							numwarn, numalert, numcrit);
				}
			}
			else if (! ascsv)
			{
				printf ("    </members>\n");
				printf ("    <summary>\n");
				printf ("      <netin>%u</netin>\n", netin);
				printf ("      <netout>%u</netout>\n", netout);
				printf ("      <rtt>%.1f</rtt>\n",
						((double) rtt / (10.0 * ((double) count))));
				printf ("      <counts.warning>%i</counts.warning>\n", numwarn);
				printf ("      <counts.alert>%i</counts.alert>\n", numalert);
				printf ("      <counts.critical>%i</counts.critical>\n",
						numcrit);
				printf ("    </summary>\n");
				printf ("  </group>\n");
			}
		}
		grp = grp->next;
	}
	if (asxml)
	{
		printf ("</nl.madscience.svc.n2.groups>\n");
	}
	if (asjson) printf ("}\n");
	exit(0);
}
