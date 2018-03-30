/*
 *  git2r, R bindings to the libgit2 library.
 *  Copyright (C) 2013-2018 The git2r contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2,
 *  as published by the Free Software Foundation.
 *
 *  git2r is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <git2.h>

#include "git2r_arg.h"
#include "git2r_error.h"
#include "git2r_odb.h"
#include "git2r_repository.h"

/**
 * Determine the sha of character vectors without writing to the
 * object data base.
 *
 * @param data STRSXP with character vectors to hash
 * @return A STRSXP with character vector of sha values
 */
SEXP git2r_odb_hash(SEXP data)
{
    SEXP result;
    int error = GIT_OK;
    size_t len, i;
    char sha[GIT_OID_HEXSZ + 1];
    git_oid oid;

    if (git2r_arg_check_string_vec(data))
        git2r_error(__func__, NULL, "'data'", git2r_err_string_vec_arg);

    len = Rf_length(data);
    PROTECT(result = Rf_allocVector(STRSXP, len));
    for (i = 0; i < len; i++) {
        if (NA_STRING == STRING_ELT(data, i)) {
            SET_STRING_ELT(result, i, NA_STRING);
        } else {
            error = git_odb_hash(&oid,
                               CHAR(STRING_ELT(data, i)),
                               LENGTH(STRING_ELT(data, i)),
                               GIT_OBJ_BLOB);
            if (error)
                break;

            git_oid_fmt(sha, &oid);
            sha[GIT_OID_HEXSZ] = '\0';
            SET_STRING_ELT(result, i, Rf_mkChar(sha));
        }
    }

    UNPROTECT(1);

    if (error)
        git2r_error(__func__, giterr_last(), NULL, NULL);

    return result;
}

/**
 * Determine the sha of files without writing to the object data
 * base.
 *
 * @param path STRSXP with file vectors to hash
 * @return A STRSXP with character vector of sha values
 */
SEXP git2r_odb_hashfile(SEXP path)
{
    SEXP result;
    int error = GIT_OK;
    size_t len, i;
    char sha[GIT_OID_HEXSZ + 1];
    git_oid oid;

    if (git2r_arg_check_string_vec(path))
        git2r_error(__func__, NULL, "'path'", git2r_err_string_vec_arg);

    len = Rf_length(path);
    PROTECT(result = Rf_allocVector(STRSXP, len));
    for (i = 0; i < len; i++) {
        if (NA_STRING == STRING_ELT(path, i)) {
            SET_STRING_ELT(result, i, NA_STRING);
        } else {
            error = git_odb_hashfile(&oid,
                                   CHAR(STRING_ELT(path, i)),
                                   GIT_OBJ_BLOB);
            if (error)
                break;

            git_oid_fmt(sha, &oid);
            sha[GIT_OID_HEXSZ] = '\0';
            SET_STRING_ELT(result, i, Rf_mkChar(sha));
        }
    }

    UNPROTECT(1);

    if (error)
        git2r_error(__func__, giterr_last(), NULL, NULL);

    return result;
}

/**
 * Data structure to hold information when iterating over objects.
 */
typedef struct {
    size_t n;
    SEXP list;
    git_odb *odb;
} git2r_odb_objects_cb_data;

/**
 * Add object
 *
 * @param oid Oid of the object
 * @param list The list to hold the S3 class git_object
 * @param i The index to the list item
 * @param type The type of the object
 * @param len The length of the object
 * @param repo The S3 class that contains the object
 * @return void
 */
static void git2r_add_object(
    const git_oid *oid,
    SEXP list,
    size_t i,
    const char *type,
    size_t len)
{
    int j = 0;
    char sha[GIT_OID_HEXSZ + 1];

    /* Sha */
    git_oid_fmt(sha, oid);
    sha[GIT_OID_HEXSZ] = '\0';
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(sha));

    /* Type */
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(type));

    /* Length */
    INTEGER(VECTOR_ELT(list, j++))[i] = len;
}

/**
 * Callback when iterating over objects
 *
 * @param oid Oid of the object
 * @param payload Payload data
 * @return int 0 or error code
 */
static int git2r_odb_objects_cb(const git_oid *oid, void *payload)
{
    int error;
    size_t len;
    git_otype type;
    git2r_odb_objects_cb_data *p = (git2r_odb_objects_cb_data*)payload;

    error = git_odb_read_header(&len, &type, p->odb, oid);
    if (error)
        return error;

    switch(type) {
    case GIT_OBJ_COMMIT:
        if (!Rf_isNull(p->list))
            git2r_add_object(oid, p->list, p->n, "commit", len);
        break;
    case GIT_OBJ_TREE:
        if (!Rf_isNull(p->list))
            git2r_add_object(oid, p->list, p->n, "tree", len);
        break;
    case GIT_OBJ_BLOB:
        if (!Rf_isNull(p->list))
            git2r_add_object(oid, p->list, p->n, "blob", len);
        break;
    case GIT_OBJ_TAG:
        if (!Rf_isNull(p->list))
            git2r_add_object(oid, p->list, p->n, "tag", len);
        break;
    default:
        return 0;
    }

    p->n += 1;

    return 0;
}

/**
 * List all objects available in the database
 *
 * @param repo S3 class git_repository
 * @return list with sha's for commit's, tree's, blob's and tag's
 */
SEXP git2r_odb_objects(SEXP repo)
{
    int i, error, nprotect = 0;
    SEXP result = R_NilValue;
    SEXP names = R_NilValue;
    git2r_odb_objects_cb_data cb_data = {0, R_NilValue, NULL};
    git_odb *odb = NULL;
    git_repository *repository = NULL;

    repository = git2r_repository_open(repo);
    if (!repository)
        git2r_error(__func__, NULL, git2r_err_invalid_repository, NULL);

    error = git_repository_odb(&odb, repository);
    if (error)
        goto cleanup;
    cb_data.odb = odb;

    /* Count number of objects before creating the list */
    error = git_odb_foreach(odb, &git2r_odb_objects_cb, &cb_data);
    if (error)
        goto cleanup;

    PROTECT(result = Rf_allocVector(VECSXP, 3));
    nprotect++;
    Rf_setAttrib(result, R_NamesSymbol, names = Rf_allocVector(STRSXP, 3));

    i = 0;
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("sha"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("type"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(INTSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("len"));

    cb_data.list = result;
    cb_data.n = 0;
    error = git_odb_foreach(odb, &git2r_odb_objects_cb, &cb_data);

cleanup:
    git_repository_free(repository);
    git_odb_free(odb);

    if (nprotect)
        UNPROTECT(nprotect);

    if (error)
        git2r_error(__func__, giterr_last(), NULL, NULL);

    return result;
}

/**
 * Data structure to hold information when iterating over blobs.
 */
typedef struct {
    size_t n;
    SEXP list;
    git_repository *repository;
    git_odb *odb;
} git2r_odb_blobs_cb_data;

/**
 * Add blob entry to list
 *
 * @param entry The tree entry (blob) to add
 * @param odb The object database
 * @param list The list to hold the blob information
 * @param i The vector index of the list items to use for the blob information
 * @param path The path to the tree relative to the repository workdir
 * @param commit The commit that contains the root tree of the iteration
 * @param author The author of the commit
 * @param when Time of the commit
 * @return 0 or error code
 */
static int git2r_odb_add_blob(
    const git_tree_entry *entry,
    git_odb *odb,
    SEXP list,
    size_t i,
    const char *path,
    const char *commit,
    const char *author,
    double when)
{
    int error;
    int j = 0;
    size_t len;
    git_otype type;
    char sha[GIT_OID_HEXSZ + 1];

    /* Sha */
    git_oid_fmt(sha, git_tree_entry_id(entry));
    sha[GIT_OID_HEXSZ] = '\0';
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(sha));

    /* Path */
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(path));

    /* Name */
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(git_tree_entry_name(entry)));

    /* Length */
    error = git_odb_read_header(&len, &type, odb, git_tree_entry_id(entry));
    if (error)
        return error;
    INTEGER(VECTOR_ELT(list, j++))[i] = len;

    /* Commit sha */
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(commit));

    /* Author */
    SET_STRING_ELT(VECTOR_ELT(list, j++), i, Rf_mkChar(author));

    /* When */
    REAL(VECTOR_ELT(list, j++))[i] = when;

    return GIT_OK;
}

/**
 * Recursively iterate over all tree's
 *
 * @param tree The tree to iterate over
 * @param path The path to the tree relative to the repository workdir
 * @param commit The commit that contains the root tree of the iteration
 * @param author The author of the commit
 * @param when Time of the commit
 * @param data The callback data when iterating over odb objects
 * @return 0 or error code
 */
static int git2r_odb_tree_blobs(
    const git_tree *tree,
    const char *path,
    const char *commit,
    const char *author,
    double when,
    git2r_odb_blobs_cb_data *data)
{
    int error;
    size_t i, n;

    n = git_tree_entrycount(tree);
    for (i = 0; i < n; ++i) {
        const git_tree_entry *entry;

        entry = git_tree_entry_byindex(tree, i);
        switch (git_tree_entry_type(entry)) {
        case GIT_OBJ_TREE:
        {
            char *buf = NULL;
            size_t buf_len;
            const char *sep;
            git_tree *sub_tree = NULL;

            error = git_tree_lookup(
                &sub_tree,
                data->repository,
                git_tree_entry_id(entry));
            if (error)
                return error;

            buf_len = strlen(path) + strlen(git_tree_entry_name(entry)) + 2;
            buf = malloc(buf_len);
            if (strlen(path)) {
                sep = "/";
            } else {
                sep = "";
            }
            error = snprintf(buf, buf_len, "%s%s%s", path, sep, git_tree_entry_name(entry));

            if (error >= 0 && error < buf_len) {
                error = git2r_odb_tree_blobs(
                    sub_tree,
                    buf,
                    commit,
                    author,
                    when,
                    data);
            }

            free(buf);
            git_tree_free(sub_tree);

            if (error)
                return error;

            break;
        }
        case GIT_OBJ_BLOB:
            if (!Rf_isNull(data->list)) {
                error = git2r_odb_add_blob(
                    entry,
                    data->odb,
                    data->list,
                    data->n,
                    path,
                    commit,
                    author,
                    when);
                if (error)
                    return error;
            }
            data->n += 1;
            break;
        default:
            break;
        }
    }

    return GIT_OK;
}

/**
 * Callback when iterating over blobs
 *
 * @param oid Oid of the object
 * @param payload Payload data
 * @return int 0 or error code
 */
static int git2r_odb_blobs_cb(const git_oid *oid, void *payload)
{
    int error = GIT_OK;
    size_t len;
    git_otype type;
    git2r_odb_blobs_cb_data *p = (git2r_odb_blobs_cb_data*)payload;

    error = git_odb_read_header(&len, &type, p->odb, oid);
    if (error)
        return error;

    if (GIT_OBJ_COMMIT == type) {
        const git_signature *author;
        git_commit *commit = NULL;
        git_tree *tree = NULL;
        char sha[GIT_OID_HEXSZ + 1];

        error = git_commit_lookup(&commit, p->repository, oid);
        if (error)
            goto cleanup;

        error = git_commit_tree(&tree, commit);
        if (error)
            goto cleanup;

        git_oid_fmt(sha, oid);
        sha[GIT_OID_HEXSZ] = '\0';

        author = git_commit_author(commit);

        /* Recursively iterate over all tree's */
        error = git2r_odb_tree_blobs(
            tree,
            "",
            sha,
            author->name,
            (double)(author->when.time) + 60 * (double)(author->when.offset),
            p);

    cleanup:
        git_commit_free(commit);
        git_tree_free(tree);
    }

    return error;
}

/**
 * List all blobs available in the database
 *
 * List all blobs reachable from the commits in the object
 * database. First list all commits. Then iterate over each blob from
 * the tree and sub-trees of each commit.
 * @param repo S3 class git_repository
 * @return A list with blob entries
 */
SEXP git2r_odb_blobs(SEXP repo)
{
    int i, error, nprotect = 0;
    SEXP result = R_NilValue;
    SEXP names = R_NilValue;
    git2r_odb_blobs_cb_data cb_data = {0, R_NilValue, NULL, NULL};
    git_odb *odb = NULL;
    git_repository *repository = NULL;

    repository = git2r_repository_open(repo);
    if (!repository)
        git2r_error(__func__, NULL, git2r_err_invalid_repository, NULL);

    error = git_repository_odb(&odb, repository);
    if (error)
        goto cleanup;
    cb_data.odb = odb;

    /* Count number of blobs before creating the list */
    cb_data.repository = repository;
    error = git_odb_foreach(odb, &git2r_odb_blobs_cb, &cb_data);
    if (error)
        goto cleanup;

    PROTECT(result = Rf_allocVector(VECSXP, 7));
    nprotect++;
    Rf_setAttrib(result, R_NamesSymbol, names = Rf_allocVector(STRSXP, 7));

    i = 0;
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("sha"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("path"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("name"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(INTSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("len"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("commit"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(STRSXP,  cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("author"));
    SET_VECTOR_ELT(result, i,   Rf_allocVector(REALSXP, cb_data.n));
    SET_STRING_ELT(names,  i++, Rf_mkChar("when"));

    cb_data.list = result;
    cb_data.n = 0;
    error = git_odb_foreach(odb, &git2r_odb_blobs_cb, &cb_data);

cleanup:
    git_repository_free(repository);
    git_odb_free(odb);

    if (nprotect)
        UNPROTECT(nprotect);

    if (error)
        git2r_error(__func__, giterr_last(), NULL, NULL);

    return result;
}
