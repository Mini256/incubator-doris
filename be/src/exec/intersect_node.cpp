// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "exec/intersect_node.h"

#include "exec/hash_table.hpp"
#include "exprs/expr.h"
#include "runtime/row_batch.h"
#include "runtime/runtime_state.h"
#include "runtime/thread_context.h"

namespace doris {
IntersectNode::IntersectNode(ObjectPool* pool, const TPlanNode& tnode, const DescriptorTbl& descs)
        : SetOperationNode(pool, tnode, descs, tnode.intersect_node.tuple_id) {}

Status IntersectNode::init(const TPlanNode& tnode, RuntimeState* state) {
    RETURN_IF_ERROR(SetOperationNode::init(tnode, state));
    // Create result_expr_ctx_lists_ from thrift exprs.
    auto& result_texpr_lists = tnode.intersect_node.result_expr_lists;
    for (auto& texprs : result_texpr_lists) {
        std::vector<ExprContext*> ctxs;
        RETURN_IF_ERROR(Expr::create_expr_trees(_pool, texprs, &ctxs));
        _child_expr_lists.push_back(ctxs);
    }
    return Status::OK();
}

// the actual intersect operation is in this function,
// 1  build a hash table from child(0)
// 2 probe with child(1), then filter the hash table and find the matched item, use them to rebuild a hash table
// repeat [2] this for all the rest child
Status IntersectNode::open(RuntimeState* state) {
    RETURN_IF_ERROR(SetOperationNode::open(state));
    SCOPED_SWITCH_THREAD_LOCAL_MEM_TRACKER_ERR_CB("Intersect Node, while probing the hash table.");
    // if a table is empty, the result must be empty
    if (_hash_tbl->size() == 0) {
        _hash_tbl_iterator = _hash_tbl->begin();
        return Status::OK();
    }
    bool eos = false;

    for (int i = 1; i < _children.size(); ++i) {
        if (i > 1) {
            RETURN_IF_ERROR(refresh_hash_table<true>(i));
        }

        _valid_element_in_hash_tbl = 0;
        // probe
        _probe_batch.reset(
                new RowBatch(child(i)->row_desc(), state->batch_size()));
        ScopedTimer<MonotonicStopWatch> probe_timer(_probe_timer);
        RETURN_IF_ERROR(child(i)->open(state));
        eos = false;
        while (!eos) {
            RETURN_IF_CANCELLED(state);
            RETURN_IF_ERROR(child(i)->get_next(state, _probe_batch.get(), &eos));
            for (int j = 0; j < _probe_batch->num_rows(); ++j) {
                _hash_tbl_iterator = _hash_tbl->find(_probe_batch->get_row(j));
                if (_hash_tbl_iterator != _hash_tbl->end()) {
                    if (!_hash_tbl_iterator.matched()) {
                        _valid_element_in_hash_tbl++;
                        _hash_tbl_iterator.set_matched();
                    }
                }
            }
            _probe_batch->reset();
        }
    }
    _hash_tbl_iterator = _hash_tbl->begin();
    return Status::OK();
}

Status IntersectNode::get_next(RuntimeState* state, RowBatch* out_batch, bool* eos) {
    RETURN_IF_ERROR(exec_debug_action(TExecNodePhase::GETNEXT));
    RETURN_IF_CANCELLED(state);
    SCOPED_TIMER(_runtime_profile->total_time_counter());
    *eos = true;
    if (reached_limit()) {
        return Status::OK();
    }
    int64_t tuple_buf_size;
    uint8_t* tuple_buf;
    RETURN_IF_ERROR(
            out_batch->resize_and_allocate_tuple_buffer(state, &tuple_buf_size, &tuple_buf));
    memset(tuple_buf, 0, tuple_buf_size);
    while (_hash_tbl_iterator.has_next()) {
        if (_hash_tbl_iterator.matched()) {
            create_output_row(_hash_tbl_iterator.get_row(), out_batch, tuple_buf);
            tuple_buf += _tuple_desc->byte_size();
            ++_num_rows_returned;
        }
        _hash_tbl_iterator.next<false>();
        *eos = !_hash_tbl_iterator.has_next() || reached_limit();
        if (out_batch->is_full() || out_batch->at_resource_limit() || *eos) {
            return Status::OK();
        }
    }
    return Status::OK();
}
} // namespace doris