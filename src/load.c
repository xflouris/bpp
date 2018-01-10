/*
    Copyright (C) 2016-2017 Tomas Flouri and Ziheng Yang

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Contact: Tomas Flouri <Tomas.Flouri@h-its.org>,
    Heidelberg Institute for Theoretical Studies,
    Schloss-Wolfsbrunnenweg 35, D-69118 Heidelberg, Germany
*/

#include "bpp.h"

#define PROP_COUNT 5

#define LOAD(x,n,fp) (fread((void *)(x),sizeof(*(x)),n,fp) == (size_t)(n))

size_t chk_size_int;
size_t chk_size_long;
size_t chk_size_double;

static BYTE dummy[256] = {0};

static stree_t * stree;
static gtree_t ** gtree;
static locus_t ** locus;

static void alloc_gtree()
{
  long i,j;

  /* initialize gene trees */
  gtree = (gtree_t **)xmalloc((size_t)opt_nloci * sizeof(gtree_t *));

  /* allocate gene tree structures */
  for (i = 0; i < opt_nloci; ++i)
  {
    gtree[i] = (gtree_t *)xmalloc(sizeof(gtree_t));
    gtree_t * gt = gtree[i];

    gt->tip_count = 0;

    /* get number of gene tree tips by looking in the number of incoming
       sequences in the species tree tip nodes */
    for (j = 0; j < stree->tip_count; ++j)
      gt->tip_count += stree->nodes[j]->seqin_count[i];

    printf("Gene tree %ld tip nodes %d\n", i, gt->tip_count);

    gt->inner_count = gt->tip_count-1;
    gt->edge_count = 2*gt->tip_count-2;
    gt->root = NULL;

    gt->nodes = (gnode_t **)xmalloc((size_t)(2*gt->tip_count-1) *
                                    sizeof(gnode_t *));
    
    for (j = 0; j < gt->tip_count + gt->inner_count; ++j)
    {
      gt->nodes[j] = (gnode_t *)xmalloc(sizeof(gnode_t));

      gt->nodes[j]->node_index = j;
    }
  }
}

void load_chk_header(FILE * fp)
{
  long version_major;
  long version_minor;
  long version_patch;
  long version_chkp;
  BYTE magic[BPP_MAGIC_BYTES];
  BYTE buffer[16];

  if (!LOAD(magic,BPP_MAGIC_BYTES,fp))
    fatal(" Magic header mismatch");

  if (!LOAD(buffer,16,fp))
    fatal("Cannot read version information");


  version_major = (buffer[0]  <<  0) | 
                  (buffer[1]  <<  8) |
                  (buffer[2]  << 16) |
                  (buffer[3]  << 24);

  version_minor = (buffer[4]  <<  0) | 
                  (buffer[5]  <<  8) |
                  (buffer[6]  << 16) |
                  (buffer[7]  << 24);
 
  version_patch = (buffer[8]  <<  0) | 
                  (buffer[9]  <<  8) |
                  (buffer[10] << 16) |
                  (buffer[11] << 24);

  version_chkp  = (buffer[12] <<  0) | 
                  (buffer[13] <<  8) |
                  (buffer[14] << 16) |
                  (buffer[15] << 24);

  printf(" Magic: %c%c%c%c\n", magic[0],magic[1],magic[2],magic[3]);
  printf(" Version: %ld.%ld.%ld\n", version_major, version_minor, version_patch);
  printf(" CHKP version: %ld\n", version_chkp);

  if (memcmp(magic,BPP_MAGIC,BPP_MAGIC_BYTES))
    fatal("File %s is not a BPP checkpoint file...", opt_resume);

  if ((version_major != VERSION_MAJOR) || (version_minor != VERSION_MINOR) || (version_patch != VERSION_PATCH))
    fatal("Incompatible CHKP: Checkpoint file version %ld, BPP version %ld",
          version_chkp, VERSION_CHKP);

  if (!LOAD(buffer,3,fp))
    fatal("Cannot read data type sizes");

  chk_size_int    = buffer[0];
  chk_size_long   = buffer[1];
  chk_size_double = buffer[2];

  /* TODO: Note that MSVC supports the %z only from verison 2015 (cl v19) */
  printf(" Type sizes: int(%zu) long(%zu) double(%zu)\n",
         chk_size_int, chk_size_long, chk_size_double);

  if (sizeof(int) != chk_size_int)
    fatal("Mismatching int size");
  if (sizeof(long) != chk_size_long)
    fatal("Mismatching long size");
  if (sizeof(double) != chk_size_double)
    fatal("Mismatching double size");

  unsigned int rng_state;
  unsigned int sections;
  unsigned long size_section;

  if (!LOAD(&rng_state,1,fp))
    fatal("Cannot read RNG state");
  printf(" RNG state: %u\n", rng_state);
  set_legacy_rndu_status(rng_state);

  if (!LOAD(&sections,1,fp))
    fatal("Cannot read number of sections");
  printf(" Sections: %u\n", sections);

  if (!LOAD(&size_section,1,fp))
    fatal("Cannot read number of sections");
  printf("   Section 1: %ld bytes\n\n", size_section);
}

int load_string(FILE * fp, char ** buffer)
{
  size_t alloc = 0;
  size_t size = 0;
  size_t increment = 100;
  int c = 0;

  char * s = (char *)xmalloc(increment*sizeof(char));
  alloc = increment;

  while ((c=fgetc(fp)) && c != EOF)
  {
    s[size++] = (char)c;

    if (size == alloc)
    {
      char * temp = (char *)xmalloc((alloc+increment)*sizeof(char));
      memcpy(temp,s,size*sizeof(char));
      free(s);
      s = temp;
    }
  }
  s[size] = 0;

  char * temp = (char *)xmalloc((size+1)*sizeof(char));
  memcpy(temp,s,(size+1)*sizeof(char));
  free(s);

  *buffer = temp;

  if ((c == EOF) || size == 0) return 0;

  return 1;
}

static void load_chk_section_1(FILE * fp,
                               double * pjump,
                               unsigned long * curstep,
                               long * ft_round,
                               long * mcmc_offset)
{
  long i;

  if (!LOAD(&opt_seed,1,fp))
    fatal("Cannot read seed");
  printf(" Seed: %ld\n", opt_seed);

  /* read MSA filename */
  if (!load_string(fp,&opt_msafile))
    fatal("Cannot read name of MSA file");
  printf(" MSA file: %s\n", opt_msafile);

  /* read imap filename */
  if (!load_string(fp,&opt_mapfile))
    fatal("Cannot read name of map file");
  printf(" Map file: %s\n", opt_mapfile);

  /* read output filename */
  if (!load_string(fp,&opt_outfile))
    fatal("Cannot read name of output file");
  printf(" Output file: %s\n", opt_outfile);

  /* read output filename */
  if (!load_string(fp,&opt_mcmcfile))
    fatal("Cannot read name of mcmc file");
  printf(" MCMC file: %s\n", opt_mcmcfile);

  /* read speciesdelimitation */
  if (!LOAD(&opt_delimit,1,fp))
    fatal("Cannot read 'speciesdelimitation' tag");

  if (!LOAD(&opt_rjmcmc_method,1,fp))
    fatal("Cannot read 'speciesdelimitation' tag");

  if (opt_delimit && opt_rjmcmc_method != 0 && opt_rjmcmc_method != 1)
    fatal("rj-MCMC method can be either 0 or 1, but found %ld", opt_rjmcmc_method);

  if (opt_rjmcmc_method == 0)
  {
    if (!LOAD(&opt_rjmcmc_epsilon,1,fp))
      fatal("Cannot read 'speciesdelimitation' tag");
    if (!LOAD(dummy,sizeof(double),fp))
      fatal("Cannot read 'speciesdelimitation' tag");
    if (opt_delimit)
      printf(" Speciesdelimitation: %ld %ld %f\n", opt_delimit, opt_rjmcmc_method, opt_rjmcmc_epsilon);
    else
      printf(" Speciesdelimitation: Disabled\n");
  }
  else
  {
    if (!LOAD(&opt_rjmcmc_alpha,1,fp))
      fatal("Cannot read 'speciesdelimitation' tag");
    if (!LOAD(&opt_rjmcmc_mean,1,fp))
      fatal("Cannot read 'speciesdelimitation' tag");
    if (opt_delimit)
      printf(" Speciesdelimitation: %ld %ld %f %f\n", opt_delimit, opt_rjmcmc_method, opt_rjmcmc_alpha, opt_rjmcmc_mean);
    else
      printf(" Speciesdelimitation: Disabled\n");
  }

  /* read speciestree */
  if (!LOAD(&opt_stree,1,fp))
    fatal("Cannot read 'speciestree' tag");
  printf(" Speciestree: %ld\n", opt_stree);

  if (!LOAD(dummy,3*sizeof(double),fp))
    fatal("Cannot read 'speciestree' tag");

  /* read speciesmodelprior */
  if (!LOAD(&opt_delimit_prior,1,fp))
    fatal("Cannot read 'speciesmodelprior' tag");
  printf(" Speciesmodelprior: %ld\n", opt_delimit_prior);

  /* read species&tree */
  unsigned int stree_tip_count;
  if (!LOAD(&stree_tip_count,1,fp))
    fatal("Cannot read 'species&tree' tag");

  char ** labels = (char **)xmalloc((size_t)stree_tip_count*sizeof(char *));
  for (i = 0; i < stree_tip_count; ++i)
  {
    if (!load_string(fp,labels+i))
      fatal("Cannot read species labels for 'species&tree' tag");
  }
  printf(" Species&tree: %u species (", stree_tip_count);
  for (i = 0; i < stree_tip_count; ++i)
    printf(" %s", labels[i]);
  printf(")\n");

  /* read usedata, cleandata and nloci */
  if (!LOAD(&opt_usedata,1,fp))
    fatal("Cannot read 'usedata' tag");
  printf(" usedata: %ld\n", opt_usedata);
  if (!LOAD(&opt_cleandata,1,fp))
    fatal("Cannot read 'cleandata' tag");
  printf(" cleandata: %ld\n", opt_cleandata);
  if (!LOAD(&opt_nloci,1,fp))
    fatal("Cannot read 'nloci' tag");
  printf(" nloci: %ld\n", opt_nloci);

  /* read theta prior */
  if (!LOAD(&opt_theta_alpha,1,fp))
    fatal("Cannot read alpha of 'theta' tag");
  if (!LOAD(&opt_theta_beta,1,fp))
    fatal("Cannot read beta 'theta' tag");
  if (!LOAD(&opt_est_theta,1,fp))
    fatal("Cannot read est 'theta' tag");
  printf(" theta: %f %f %ld\n", opt_theta_alpha, opt_theta_beta, opt_est_theta);
  
  /* read tau prior */
  if (!LOAD(&opt_tau_alpha,1,fp))
    fatal("Cannot read alpha of 'theta' tag");
  if (!LOAD(&opt_tau_beta,1,fp))
    fatal("Cannot read beta 'theta' tag");
  printf(" tau: %f %f\n", opt_tau_alpha, opt_tau_beta);

  /* read finetune */
  if (!LOAD(&opt_finetune_reset,1,fp))
    fatal("Cannot read 'finetune' tag");
  if (!LOAD(&opt_finetune_gtage,1,fp))
    fatal("Cannot read gene tree age finetune parameter");
  if (!LOAD(&opt_finetune_gtspr,1,fp))
    fatal("Cannot read gene tree SPR finetune parameter");
  if (!LOAD(&opt_finetune_theta,1,fp))
    fatal("Cannot read species tree theta finetune parameter");
  if (!LOAD(&opt_finetune_tau,1,fp))
    fatal("Cannot read species tree tau finetune parameter");
  if (!LOAD(&opt_finetune_mix,1,fp))
    fatal("Cannot read species mixing step finetune parameter");
  printf(" Current finetune: %ld: %f %f %f %f %f\n", opt_finetune_reset, opt_finetune_gtage, opt_finetune_gtspr,
         opt_finetune_theta, opt_finetune_tau,opt_finetune_mix);

  /* read diploid */
  opt_diploid = (long *)xmalloc((size_t)stree_tip_count*sizeof(char *));
  if (!LOAD(opt_diploid,stree_tip_count,fp))
    fatal("Cannot read 'diploid' tag");

  for  (i = 0; i < stree_tip_count; ++i)
    if (opt_diploid[i]) break;

  if (i == stree_tip_count)  /* no diploids */
  {
    free(opt_diploid);
    opt_diploid = NULL;
    printf(" Diploid: None found\n");
  }
  else
  {
    printf(" Diploid:");
    for (i = 0; i < stree_tip_count; ++i)
      printf(" %ld", opt_diploid[i]);
    printf("\n");
  }

  /* read MCMC run info  */
  if (!LOAD(&opt_burnin,1,fp))
    fatal("Cannot read 'burnin' tag");
  if (!LOAD(&opt_samplefreq,1,fp))
    fatal("Cannot read 'sampfreq' tag");
  if (!LOAD(&opt_samples,1,fp))
    fatal("Cannot read 'nsample' tag");
  if (!LOAD(curstep,1,fp))
    fatal("Cannot read current MCMC step");
  if (!LOAD(ft_round,1,fp))
    fatal("Cannot read current finetune round");

  if (!LOAD(pjump,PROP_COUNT,fp))
    fatal("Cannot read pjump");

  if (!LOAD(mcmc_offset,1,fp))
    fatal("Cannot read current finetune round");

  fprintf(stdout, " Burnin: %ld\n", opt_burnin);
  fprintf(stdout, " Sampfreq: %ld\n", opt_samplefreq);
  fprintf(stdout, " Nsample: %ld\n", opt_samples);
  fprintf(stdout, " Next step: %ld\n\n", *curstep);

  /* populate species tree */
  stree = (stree_t *)xmalloc(sizeof(stree_t));
  
  stree->tip_count = stree_tip_count;
  stree->inner_count = stree_tip_count-1;
  stree->edge_count = stree->tip_count + stree->inner_count - 1;
  stree->locus_count = opt_nloci;

  stree->nodes = (snode_t **)xmalloc((size_t)(stree->tip_count + stree->inner_count)*sizeof(snode_t *));
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    stree->nodes[i] = (snode_t *)xmalloc(sizeof(snode_t));


  /* set tip labels */
  for (i = 0; i < stree->tip_count; ++i)
    stree->nodes[i]->label = labels[i];
  free(labels);

  for (i = 0; i < stree->inner_count; ++i)
    stree->nodes[stree->tip_count+i]->label = NULL;

  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
  {
    stree->nodes[i]->node_index = i;
    stree->nodes[i]->data = NULL;
  }

  /* allocate coalescent events */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
  {
    stree->nodes[i]->event_count = (int *)xmalloc((size_t)opt_nloci*sizeof(int));
    stree->nodes[i]->seqin_count = (int *)xmalloc((size_t)opt_nloci*sizeof(int));
    stree->nodes[i]->gene_leaves = (unsigned int *)xmalloc((size_t)opt_nloci*sizeof(unsigned int));

    stree->nodes[i]->logpr_contrib = (double *)xmalloc((size_t)opt_nloci*sizeof(double));
    stree->nodes[i]->old_logpr_contrib = (double *)xmalloc((size_t)opt_nloci*sizeof(double));

    stree->nodes[i]->event = (dlist_t **)xmalloc((size_t)opt_nloci *
                                                 sizeof(dlist_t *));
  }

  stree->pptable = (int **)xcalloc((size_t)(stree->tip_count + stree->inner_count),
                                   sizeof(int *));
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    stree->pptable[i] = (int *)xcalloc((size_t)(stree->tip_count + stree->inner_count),
                                       sizeof(int));
}

void load_chk_section_2(FILE * fp)
{
  long i,j,k;

  /* reset parent nodes to NULL */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    stree->nodes[i]->parent = NULL;

  /* reset child nodes to NULL for tips */
  for (i = 0; i < stree->tip_count; ++i)
    stree->nodes[i]->left = stree->nodes[i]->right = NULL;

  /* set left child for each inner node according to checkpoint data */
  for (i = 0; i < stree->inner_count; ++i)
  {
    unsigned int left_child_index;
    if (!LOAD(&left_child_index,1,fp))
      fatal("Cannot read species tree topology");
    stree->nodes[stree->tip_count+i]->left = stree->nodes[left_child_index];
    stree->nodes[left_child_index]->parent = stree->nodes[stree->tip_count+i];
  }

  /* set right child for each inner node according to checkpoint data */
  for (i = 0; i < stree->inner_count; ++i)
  {
    unsigned int right_child_index;
    if (!LOAD(&right_child_index,1,fp))
      fatal("Cannot read species tree topology");
    stree->nodes[stree->tip_count+i]->right = stree->nodes[right_child_index];
    stree->nodes[right_child_index]->parent = stree->nodes[stree->tip_count+i];
  }

  /* now set the root node */
  unsigned nullparent_count = 0;
  for (i = 0; i < stree->inner_count; ++i)
  {
    if (!stree->nodes[stree->tip_count+i]->parent)
    {
      stree->root = stree->nodes[stree->tip_count+i];
      nullparent_count++;
    }
  }
  if (nullparent_count != 1)
    fatal("Erroneous species tree structure");


  /* now check species tree consistency */
  for (i = 0; i < stree->inner_count; ++i)
    assert((stree->nodes[i]->left == stree->nodes[i]->right) &&
           (stree->nodes[i]->left == NULL));

  for (i = 0; i < stree->inner_count; ++i)
  {
    unsigned int index = stree->tip_count + i;

    assert(stree->nodes[index]->left != stree->nodes[index]->right);
    if (stree->nodes[index]->parent)
      assert(stree->nodes[index]->left != stree->nodes[index]->parent);
    assert(stree->nodes[index]->left != stree->nodes[index]);

    assert(stree->nodes[index]->left->parent == stree->nodes[index]);
    assert(stree->nodes[index]->right->parent == stree->nodes[index]);
  }

  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    stree->nodes[i]->length = 0;

  stree_label(stree);

  char * newick = stree_export_newick(stree->root,NULL);
  printf("Current specices tree: %s\n", newick);
  free(newick);

  /* read thetas */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    if (!LOAD(&(stree->nodes[i]->theta),1,fp))
      fatal("Cannot read species nodes theta");

  /* read taus */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    if (!LOAD(&(stree->nodes[i]->tau),1,fp))
      fatal("Cannot read species nodes tau");

  /* read support */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    if (!LOAD(&(stree->nodes[i]->support),1,fp))
      fatal("Cannot read species nodes support values");

  /* read number of coalescent events */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    if (!LOAD(stree->nodes[i]->event_count,opt_nloci,fp))
        fatal("Cannot read species event counts");

//  /* read MSC density contributions */
//  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
//    if (fread((void *)&(stree->nodes[i]->logpr_contrib),sizeof(double),1,fp) != 1)
//      fatal("Cannot read species MSC density contribution");

  /* read root_tau */
  if (!LOAD(&(stree->root_age),1,fp))
    fatal("Cannot read species root tau");

  /* read number of incoming sequences for each node node */
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
    if (!LOAD(stree->nodes[i]->seqin_count,opt_nloci,fp))
        fatal("Cannot read incoming sequence counts");

  alloc_gtree();
  unsigned int max_tips = 0;
  for (i = 0; i < opt_nloci; ++i)
    if (gtree[i]->tip_count > max_tips)
      max_tips = gtree[i]->tip_count;

  unsigned int gtree_inner_sum = 0;
  for (i = 0; i < opt_nloci; ++i)
    gtree_inner_sum += gtree[i]->inner_count;
  stree_alloc_internals(stree,gtree_inner_sum,opt_nloci);

  /* read event indices for each node */
  unsigned int * buffer = (unsigned int *)xmalloc((size_t)(max_tips*2-1) *
                                                  sizeof(unsigned int));
  for (i = 0; i < stree->tip_count + stree->inner_count; ++i)
  {
    snode_t * snode = stree->nodes[i];

    for (j = 0; j < opt_nloci; ++j)
    {
      snode->event[j] = dlist_create();

      if (!LOAD(buffer,snode->event_count[j],fp))
        fatal("Cannot read coalescent events");

      for (k = 0; k < snode->event_count[j]; ++k)
      {
        gnode_t * gt_node = gtree[j]->nodes[buffer[k]];

        dlist_item_t * dlitem = dlist_append(snode->event[j], (void *)gt_node);

        gt_node->event = dlitem;
      }
    }
  }

  free(buffer);
}

static void load_gene_tree(FILE * fp, long index)
{
  long i;
  unsigned int gtree_tip_count = 0;

  gtree_t * gt = gtree[index];
  

  /* get number of tips */
  for (i = 0; i < stree->tip_count; ++i)
    gtree_tip_count += stree->nodes[i]->seqin_count[index]; 

  gt->tip_count = gtree_tip_count;
  gt->inner_count = gtree_tip_count-1;
  gt->edge_count = gt->tip_count + gt->inner_count - 1;

  /* read gene tree labels */
  char ** labels = (char **)xmalloc((size_t)gtree_tip_count*sizeof(char *));
  for (i = 0; i < gtree_tip_count; ++i)
  {
    if (!load_string(fp,labels+i))
      fatal("Cannot read gene tree %ld labels", index);
  }
  printf(" Gene tree %ld: %u sequences (", index, gtree_tip_count);
  for (i = 0; i < gtree_tip_count; ++i)
  {
    printf(" %s", labels[i]);
    gt->nodes[i]->label = labels[i];
  }
  printf(")\n");

  free(labels);

  /* reset parent nodes to NULL */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    gt->nodes[i]->parent = NULL;

  /* reset data element */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    gt->nodes[i]->data = NULL;

  /* reset child nodes to NULL for tips */
  for (i = 0; i < gt->tip_count; ++i)
    gt->nodes[i]->left = gt->nodes[i]->right = NULL;

  /* set labels of inner nodes to NULL */
  for (i = 0; i < gt->inner_count; ++i)
    gt->nodes[gt->tip_count+i]->label = NULL;

  /* set left child for each inner node according to checkpoint data */
  for (i = 0; i < gt->inner_count; ++i)
  {
    unsigned int left_child_index;
    if (!LOAD(&left_child_index,1,fp))
      fatal("Cannot read species tree topology");
    gt->nodes[gt->tip_count+i]->left = gt->nodes[left_child_index];
    gt->nodes[left_child_index]->parent = gt->nodes[gt->tip_count+i];
  }

  /* set right child for each inner node according to checkpoint data */
  for (i = 0; i < gt->inner_count; ++i)
  {
    unsigned int right_child_index;
    if (!LOAD(&right_child_index,1,fp))
      fatal("Cannot read species tree topology");
    gt->nodes[gt->tip_count+i]->right = gt->nodes[right_child_index];
    gt->nodes[right_child_index]->parent = gt->nodes[gt->tip_count+i];
  }

  /* now set the root node */
  unsigned nullparent_count = 0;
  for (i = 0; i < gt->inner_count; ++i)
  {
    if (!gt->nodes[gt->tip_count+i]->parent)
    {
      gt->root = gt->nodes[gt->tip_count+i];
      nullparent_count++;
    }
  }
  if (nullparent_count != 1)
    fatal("Erroneous gene tree %ld structure", index);

  /* now check gene tree consistency */
  for (i = 0; i < gt->inner_count; ++i)
    assert((gt->nodes[i]->left == gt->nodes[i]->right) &&
           (gt->nodes[i]->left == NULL));

  for (i = 0; i < gt->inner_count; ++i)
  {
    unsigned int index = gt->tip_count + i;

    assert(gt->nodes[index]->left != gt->nodes[index]->right);
    if (gt->nodes[index]->parent)
      assert(gt->nodes[index]->left != gt->nodes[index]->parent);
    assert(gt->nodes[index]->left != gt->nodes[index]);

    assert(gt->nodes[index]->left->parent == gt->nodes[index]);
    assert(gt->nodes[index]->right->parent == gt->nodes[index]);
  }

  /* load branch lengths - TODO: Candidate for removal */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    if (!LOAD(&(gt->nodes[i]->length),1,fp))
      fatal("Cannot read gene tree branch lengths");

  /* load ages */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    if (!LOAD(&(gt->nodes[i]->time),1,fp))
      fatal("Cannot read gene tree node ages");

  /* load population index (corresponding species tree node index) */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
  {
    unsigned int pop_index;
    if (!LOAD(&(pop_index),1,fp))
      fatal("Cannot read gene tree population indices");
    gt->nodes[i]->pop = stree->nodes[pop_index];
  }

  /* load CLV indices */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    if (!LOAD(&(gt->nodes[i]->clv_index),1,fp))
      fatal("Cannot read gene tree clv indices");

  /* load scaler indices */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    if (!LOAD(&(gt->nodes[i]->scaler_index),1,fp))
      fatal("Cannot read gene tree scaler indices");

  /* load pmatrix indices */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    if (!LOAD(&(gt->nodes[i]->pmatrix_index),1,fp))
      fatal("Cannot read gene tree pmatrix indices");

  /* load mark - TODO: Candidate for removal */
  for (i = 0; i < gt->tip_count + gt->inner_count; ++i)
    if (!LOAD(&(gt->nodes[i]->mark),1,fp))
      fatal("Cannot read gene tree marks");
}

static void load_chk_section_3(FILE * fp, long msa_count)
{
  long i;

  for (i = 0; i < msa_count; ++i)
  {
    load_gene_tree(fp,i);
  }
}

static void load_locus(FILE * fp, long index)
{
  long i;

  /* load number of sites */
  unsigned int sites;
  unsigned int states;
  unsigned int rate_cats;
  unsigned int rate_matrices;
  unsigned int attributes;

  gtree_t * gt = gtree[index];

  if (!LOAD(&sites,1,fp))
    fatal("Cannot read number of sites");

  /* load number of states */
  if (!LOAD(&states,1,fp))
    fatal("Cannot read number of states");

  /* load number of rate categories */
  if (!LOAD(&rate_cats,1,fp))
    fatal("Cannot read number of rate categories");

  /* load number of rate matrices */
  if (!LOAD(&rate_matrices,1,fp))
    fatal("Cannot read number of rate matrices");

  /* load attributes */
  if (!LOAD(&attributes,1,fp))
    fatal("Cannot read attributes");

  locus[index] = locus_create(gt->tip_count,
                              2*gt->inner_count,
                              states,
                              sites,
                              rate_matrices,
                              gt->edge_count,
                              rate_cats,
                              0,
                              attributes);
  
  double frequencies[4] = {0.25, 0.25, 0.25, 0.25};

  /* set frequencies for model with index 0 */
  pll_set_frequencies(locus[index],0,frequencies);

  /* load pattern weights sum */
  if (!LOAD(&(locus[index]->pattern_weights_sum),1,fp))
    fatal("Cannot read pattern weights sum");

  /* load diploid */
  if (!LOAD(&(locus[index]->diploid),1,fp))
    fatal("Cannot read locus %ld diploid", index);
  
  if (locus[index]->diploid)
  {
    /* TODO: locus->pattern_weights is allocated in locis_create with a size
       equal to length of A3, but in reality we only need |A1| storage space.
       Free and reallocate here with 'unphased_length' */

    /* load original diploid number of sites */
    if (!LOAD(&(locus[index]->unphased_length),1,fp))
      fatal("Cannot read locus %ld unphased length", index);
    
    /* load diploid mapping A1 -> A3 */
    if (!LOAD(locus[index]->diploid_mapping,locus[index]->unphased_length,fp))
      fatal("Cannot read locus %ld diploid mapping", index);

    /* load diploid resolution count */
    if (!LOAD(locus[index]->diploid_resolution_count,locus[index]->unphased_length,fp))
      fatal("Cannot read locus %ld diploid resolution count", index);

    /* load pattern weights for original diploid A1 alignment */
    if (!LOAD(locus[index]->pattern_weights,locus[index]->unphased_length,fp))
      fatal("Cannot read pattern weights");
  }
  else
  {
    /* load pattern weights */
    if (!LOAD(locus[index]->pattern_weights,locus[index]->sites,fp))
      fatal("Cannot read pattern weights");
  }

  /* load tip CLVs */
  for (i = 0; i < gt->tip_count; ++i)
  {
    unsigned int clv_index = gt->nodes[i]->clv_index;
    size_t span = locus[index]->sites * locus[index]->states * locus[index]->rate_cats;

    if (!LOAD(locus[index]->clv[clv_index],span,fp))
      fatal("Cannot read gene tree %ld tip CLV", index);
  }
}

void load_chk_section_4(FILE * fp)
{
  long i;

  locus = (locus_t **)xmalloc((size_t)opt_nloci * sizeof(locus_t));
  for (i = 0; i < opt_nloci; ++i)
    load_locus(fp,i);

}

int checkpoint_load(gtree_t *** gtreep,
                    locus_t *** locusp,
                    stree_t ** streep,
                    double ** pjump,
                    unsigned long * curstep,
                    long * ft_round,
                    long * mcmc_offset)
{
  long i;
  FILE * fp;

  assert(opt_resume);

  fprintf(stdout, "Loading checkpoint file %s\n\n", opt_resume);
  fp = fopen(opt_resume,"r");
  if (!fp)
    fatal("Cannot open checkpoint file %s", opt_resume);

  /* read header */
  fprintf(stdout,"HEADER:\n");
  load_chk_header(fp);

  /* load section 1 */
  fprintf(stdout,"SECTION 1:\n");

  *pjump = (double *)xmalloc(PROP_COUNT*sizeof(double));
  load_chk_section_1(fp,*pjump,curstep,ft_round,mcmc_offset);

  /* load section 2 */
  load_chk_section_2(fp);

  /* initialize gene trees */
//  gtree = init_gtrees(opt_nloci);

  /* load section 3 */
  load_chk_section_3(fp,opt_nloci);

  /* load section 4 */
  load_chk_section_4(fp);

  /* TODO: set tip sequences, charmap etc when using tipchars */

  /* update pmatrices and CLVs */
  for (i = 0; i < opt_nloci; ++i)
  {
    gtree_reset_leaves(gtree[i]->root);
    locus_update_matrices_jc69(locus[i],gtree[i]->nodes,gtree[i]->edge_count);
    locus_update_all_partials(locus[i],gtree[i]);

    unsigned int param_indices[1] = {0};
    gtree[i]->logl = locus_root_loglikelihood(locus[i],
                                              gtree[i]->root,
                                              param_indices,
                                              NULL);
  }

  #if 0
  unsigned int param_indices[1] = {0};
  logl = locus_root_loglikelihood(locus[i],
                                  gtree[i]->root,
                                  param_indices,
                                  NULL);
  #endif
  fclose(fp);


  *streep = stree;
  *gtreep = gtree;
  *locusp = locus;

  return 1;
}

void checkpoint_truncate(long mcmc_offset)
{
  FILE * fp_mcmc;
  
  if (!(fp_mcmc = fopen(opt_mcmcfile, "a")))
    fatal("Cannot open file %s for reading...", opt_mcmcfile);

  if (ftruncate(fileno(fp_mcmc),mcmc_offset))
    fatal("Cannot truncate file %s to %ld bytes...", opt_mcmcfile, mcmc_offset);
  
  fclose(fp_mcmc);
}