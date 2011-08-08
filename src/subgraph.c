/*
  Copyright (C) <2009-2011> <Alexandre Xavier Falcão and João Paulo Papa>

  Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
  Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

  please see full copyright in COPYING file.

  -------------------------------------------------------------------------
  written by A.X. Falcão <afalcao@ic.unicamp.br> and by J.P. Papa
  <papa.joaopaulo@gmail.com>, Oct 20th 2008

  This program is a collection of functions to manage the Optimum-Path Forest (OPF)
  classifier.*/

#include <assert.h>
#include <time.h>

#include "common.h"
#include "metrics.h"
#include "knn.h"
#include "subgraph.h"

/*----------- Constructor and destructor ------------------------*/
// Allocate nodes without features
subgraph *
subgraph_create (int node_n)
{
  subgraph *sg = (subgraph *) calloc (1, sizeof (subgraph));
  int i;

  sg->node_n = node_n;
  sg->node = (snode *) calloc (node_n, sizeof (snode));
  sg->ordered_list_of_nodes = (int *) calloc (node_n, sizeof (int));

  if (sg->node == NULL)
    error (LOG_OUT_OF_MEMORY);

  for (i = 0; i < sg->node_n; i++)
    {
      sg->node[i].feat = NULL;
      sg->node[i].relevant = 0;
    }

  return (sg);
}

// Deallocate memory for subgraph
void
subgraph_destroy (subgraph ** sg)
{
  int i;

  if ((*sg) != NULL)
    {
      for (i = 0; i < (*sg)->node_n; i++)
        {
          if ((*sg)->node[i].feat != NULL)
            free ((*sg)->node[i].feat);
          if ((*sg)->node[i].adj != NULL)
            set_destroy (&(*sg)->node[i].adj);
        }
      free ((*sg)->node);
      free ((*sg)->ordered_list_of_nodes);
      free ((*sg));
      *sg = NULL;
    }
}

// Copy subgraph (does not copy Arcs)
subgraph *
subgraph_copy (subgraph * g)
{
  subgraph *clone = NULL;
  int i;

  if (g != NULL)
    {
      clone = subgraph_create (g->node_n);

      clone->k_best = g->k_best;
      clone->df = g->df;
      clone->label_n = g->label_n;
      clone->feat_n = g->feat_n;
      clone->dens_min = g->dens_min;
      clone->dens_max = g->dens_max;
      clone->k = g->k;

      for (i = 0; i < g->node_n; i++)
        {
          snode_copy (&clone->node[i], &g->node[i], g->feat_n);
          clone->ordered_list_of_nodes[i] = g->ordered_list_of_nodes[i];
        }

      return clone;
    }
  else
    return NULL;
}

//Copy nodes
void
snode_copy (snode * dest, snode * src, int feat_n)
{
  dest->feat = alloc_float (feat_n);
  memcpy (dest->feat, src->feat, feat_n * sizeof (float));
  dest->path_val = src->path_val;
  dest->dens = src->dens;
  dest->label = src->label;
  dest->root = src->root;
  dest->pred = src->pred;
  dest->label_true = src->label_true;
  dest->position = src->position;
  dest->status = src->status;
  dest->relevant = src->relevant;
  dest->radius = src->radius;
  dest->nplatadj = src->nplatadj;

  dest->adj = set_clone (src->adj);
}


//Swap nodes
void
snode_swap (snode * a, snode * b)
{
  snode tmp;

  tmp = *a;
  *a = *b;
  *b = tmp;
}

//Resets subgraph fields (pred and arcs)
void
subgraph_reset (subgraph * sg)
{
  int i;

  for (i = 0; i < sg->node_n; i++)
    sg->node[i].pred = NIL;
  subgraph_knn_destroy (sg);
}

//Merge two subgraphs
subgraph *
subgraph_merge (subgraph * sg1, subgraph * sg2)
{
  assert (sg1->feat_n == sg2->feat_n);

  subgraph *out = subgraph_create (sg1->node_n + sg2->node_n);
  int i = 0, j;

  if (sg1->label_n > sg2->label_n)
    out->label_n = sg1->label_n;
  else
    out->label_n = sg2->label_n;
  out->feat_n = sg1->feat_n;

  for (i = 0; i < sg1->node_n; i++)
    snode_copy (&out->node[i], &sg1->node[i], out->feat_n);
  for (j = 0; j < sg2->node_n; j++)
    {
      snode_copy (&out->node[i], &sg2->node[j], out->feat_n);
      i++;
    }

  return out;
}


//It creates k folds for cross validation
subgraph **
subgraph_k_fold (subgraph * sg, int k)
{
  subgraph **out = (subgraph **) malloc (k * sizeof (subgraph *));
  int totelems, foldsize = 0, i, *label =
    (int *) calloc ((sg->label_n + 1), sizeof (int));
  int *nelems =
    (int *) calloc ((sg->label_n + 1), sizeof (int)), j, z, w, m, n;
  int *nelems_aux = (int *) calloc ((sg->label_n + 1), sizeof (int)), *resto =
    (int *) calloc ((sg->label_n + 1), sizeof (int));

  for (i = 0; i < sg->node_n; i++)
    {
      sg->node[i].status = 0;
      label[sg->node[i].label_true]++;
    }

  for (i = 0; i < sg->node_n; i++)
    nelems[sg->node[i].label_true] =
      MAX ((int) ((1 / (float) k) * label[sg->node[i].label_true]), 1);

  for (i = 1; i <= sg->label_n; i++)
    {
      foldsize += nelems[i];
      nelems_aux[i] = nelems[i];
      resto[i] = label[i] - k * nelems_aux[i];
    }

  for (i = 0; i < k - 1; i++)
    {
      out[i] = subgraph_create (foldsize);
      out[i]->feat_n = sg->feat_n;
      out[i]->label_n = sg->label_n;
      for (j = 0; j < foldsize; j++)
        out[i]->node[j].feat = (float *) malloc (sg->feat_n * sizeof (float));
    }

  totelems = 0;
  for (j = 1; j <= sg->label_n; j++)
    totelems += resto[j];

  out[i] = subgraph_create (foldsize + totelems);
  out[i]->feat_n = sg->feat_n;
  out[i]->label_n = sg->label_n;

  for (j = 0; j < foldsize + totelems; j++)
    out[i]->node[j].feat = (float *) malloc (sg->feat_n * sizeof (float));

  for (i = 0; i < k; i++)
    {
      totelems = 0;
      if (i == k - 1)
        {
          for (w = 1; w <= sg->label_n; w++)
            {
              nelems_aux[w] += resto[w];
              totelems += nelems_aux[w];
            }
        }
      else
        {
          for (w = 1; w <= sg->label_n; w++)
            totelems += nelems_aux[w];
        }

      for (w = 1; w <= sg->label_n; w++)
        nelems[w] = nelems_aux[w];


      z = 0;
      m = 0;
      while (totelems > 0)
        {
          if (i == k - 1)
            {
              for (w = m; w < sg->node_n; w++)
                {
                  if (sg->node[w].status != NIL)
                    {
                      j = w;
                      m = w + 1;
                      break;
                    }
                }

            }
          else
            j = random_int (0, sg->node_n - 1);
          if (sg->node[j].status != NIL)
            {
              if (nelems[sg->node[j].label_true] > 0)
                {
                  out[i]->node[z].position = sg->node[j].position;
                  for (n = 0; n < sg->feat_n; n++)
                    out[i]->node[z].feat[n] = sg->node[j].feat[n];
                  out[i]->node[z].label_true = sg->node[j].label_true;
                  nelems[sg->node[j].label_true] =
                    nelems[sg->node[j].label_true] - 1;
                  sg->node[j].status = NIL;
                  totelems--;
                  z++;
                }
            }
        }
    }

  free (label);
  free (nelems);
  free (nelems_aux);
  free (resto);

  return out;
}

// Split subgraph into two parts such that the size of the first part
// is given by a percentual of samples.
void
subgraph_split (subgraph * sg, subgraph ** sg1, subgraph ** sg2,
                   float perc1)
{
  int *label = alloc_int (sg->label_n + 1), i, j, i1, i2;
  int *nelems = alloc_int (sg->label_n + 1), totelems;
  srandom ((int) time (NULL));

  for (i = 0; i < sg->node_n; i++)
    {
      sg->node[i].status = 0;
      label[sg->node[i].label_true]++;
    }

  for (i = 0; i < sg->node_n; i++)
    {
      nelems[sg->node[i].label_true] =
        MAX ((int) (perc1 * label[sg->node[i].label_true]), 1);
    }

  free (label);

  totelems = 0;
  for (j = 1; j <= sg->label_n; j++)
    totelems += nelems[j];

  *sg1 = subgraph_create (totelems);
  *sg2 = subgraph_create (sg->node_n - totelems);
  (*sg1)->feat_n = sg->feat_n;
  (*sg2)->feat_n = sg->feat_n;

  for (i1 = 0; i1 < (*sg1)->node_n; i1++)
    (*sg1)->node[i1].feat = alloc_float ((*sg1)->feat_n);
  for (i2 = 0; i2 < (*sg2)->node_n; i2++)
    (*sg2)->node[i2].feat = alloc_float ((*sg2)->feat_n);

  (*sg1)->label_n = sg->label_n;
  (*sg2)->label_n = sg->label_n;

  i1 = 0;
  while (totelems > 0)
    {
      i = random_int (0, sg->node_n - 1);
      if (sg->node[i].status != NIL)
        {
          if (nelems[sg->node[i].label_true] > 0)        // copy node to sg1
            {
              (*sg1)->node[i1].position = sg->node[i].position;
              for (j = 0; j < (*sg1)->feat_n; j++)
                (*sg1)->node[i1].feat[j] = sg->node[i].feat[j];
              (*sg1)->node[i1].label_true = sg->node[i].label_true;
              i1++;
              nelems[sg->node[i].label_true] =
                nelems[sg->node[i].label_true] - 1;
              sg->node[i].status = NIL;
              totelems--;
            }
        }
    }

  i2 = 0;
  for (i = 0; i < sg->node_n; i++)
    {
      if (sg->node[i].status != NIL)
        {
          (*sg2)->node[i2].position = sg->node[i].position;
          for (j = 0; j < (*sg2)->feat_n; j++)
            (*sg2)->node[i2].feat[j] = sg->node[i].feat[j];
          (*sg2)->node[i2].label_true = sg->node[i].label_true;
          i2++;
        }
    }

  free (nelems);
}

//normalize features
void
subgraph_normalize_features (subgraph * sg)
{
  float *mean = (float *) calloc (sg->feat_n, sizeof (float)), *std =
    (float *) calloc (sg->feat_n, sizeof (int));
  int i, j;

  for (i = 0; i < sg->feat_n; i++)
    {
      for (j = 0; j < sg->node_n; j++)
        mean[i] += sg->node[j].feat[i] / sg->node_n;
      for (j = 0; j < sg->node_n; j++)
        std[i] += pow (sg->node[j].feat[i] - mean[i], 2) / sg->node_n;
      std[i] = sqrt (std[i]);
      if (std[i] == 0)
        std[i] = 1.0;
    }

  for (i = 0; i < sg->feat_n; i++)
    {
      for (j = 0; j < sg->node_n; j++)
        sg->node[j].feat[i] = (sg->node[j].feat[i] - mean[i]) / std[i];
    }

  free (mean);
  free (std);
}

// subgraph_pdf_evaluate computation
void
subgraph_pdf_evaluate (subgraph * sg)
{
  int i, nelems;
  double dist;
  float *value = alloc_float (sg->node_n);
  set *adj = NULL;

  sg->k = (2.0 * (float) sg->df / 9.0);
  sg->dens_min = FLT_MAX;
  sg->dens_max = FLT_MIN;
  for (i = 0; i < sg->node_n; i++)
    {
      adj = sg->node[i].adj;
      value[i] = 0.0;
      nelems = 1;
      while (adj != NULL)
        {
          if (!use_precomputed_distance)
            dist =
              arc_weight (sg->node[i].feat, sg->node[adj->elem].feat,
                             sg->feat_n);
          else
            dist =
              distance_value[sg->node[i].position][sg->
                                                      node[adj->
                                                           elem].position];
          value[i] += exp (-dist / sg->k);
          adj = adj->next;
          nelems++;
        }

      value[i] = (value[i] / (float) nelems);

      if (value[i] < sg->dens_min)
        sg->dens_min = value[i];
      if (value[i] > sg->dens_max)
        sg->dens_max = value[i];
    }

  //  printf("df=%f,K1=%f,K2=%f,dens_min=%f, dens_max=%f\n",sg->df,sg->K1,sg->K2,sg->dens_min,sg->dens_max);

  if (sg->dens_min == sg->dens_max)
    {
      for (i = 0; i < sg->node_n; i++)
        {
          sg->node[i].dens = DENS_MAX;
          sg->node[i].path_val = DENS_MAX - 1;
        }
    }
  else
    {
      for (i = 0; i < sg->node_n; i++)
        {
          sg->node[i].dens =
            ((float) (DENS_MAX - 1) * (value[i] - sg->dens_min) /
             (float) (sg->dens_max - sg->dens_min)) + 1.0;
          sg->node[i].path_val = sg->node[i].dens - 1;
        }
    }
  free (value);
}


