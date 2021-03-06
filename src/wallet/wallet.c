#include <inttypes.h>
#include <stdio.h>

#include "client/api/get_funds.h"
#include "client/api/get_node_info.h"
#include "client/api/get_unspent_outputs.h"
#include "client/api/send_transaction.h"
#include "wallet/wallet.h"

static tx_inputs_t* wallet_build_inputs(wallet_t* w, unspent_outputs_t* unspent) {
  tx_inputs_t* inputs = tx_inputs_new();
  byte_t output_id[TX_OUTPUT_ID_BYTES] = {};
  unspent_outputs_t *input, *input_tmp;
  HASH_ITER(hh, unspent, input, input_tmp) {
    output_ids_t *id, *id_tmp;
    memcpy(output_id, input->addr, TANGLE_ADDRESS_BYTES);
    HASH_ITER(hh, input->ids, id, id_tmp) {
      memcpy(output_id + TANGLE_ADDRESS_BYTES, id->id, TX_ID_BYTES);
      tx_inputs_push(inputs, output_id);
    }
  }

  return inputs;
}

static tx_outputs_t* wallet_build_outputs(wallet_t* w, send_funds_op_t* dest, unspent_outputs_t* unspent) {
  tx_outputs_t* outputs = tx_outputs_new();
  uint64_t output_balance = unspent_outputs_balance_with_color(&unspent, dest->color);
  bool recv_eq_remainder = false;
  // is the remainder needed?
  if (output_balance > dest->amount) {
    if (empty_byte_array(dest->remainder, TANGLE_ADDRESS_BYTES)) {
      tx_output_t out = {};
      for (uint64_t i = w->addr_manager->first_unspent_idx; i <= w->addr_manager->last_addr_index; i++) {
        address_get(w->addr_manager->seed, i, ADDRESS_VER_ED25519, dest->remainder);
        if (unspent_outputs_find(&unspent, dest->remainder) == NULL) {
          out.addr_index = i;
          break;
        }
        if (i == w->addr_manager->last_addr_index) {
          am_get_new_address(w->addr_manager, dest->remainder);
          out.addr_index = w->addr_manager->last_addr_index + 1;
        }
      }

      memcpy(out.address, dest->remainder, TANGLE_ADDRESS_BYTES);

      // create balance
      balance_t balance = {};
      memcpy(balance.color, dest->color, BALANCE_COLOR_BYTES);

      if (memcmp(dest->receiver, dest->remainder, TANGLE_ADDRESS_BYTES) == 0) {
        // put all outputs in the same address
        recv_eq_remainder = true;
        balance.value = (int64_t)output_balance;
      } else {
        balance.value = (int64_t)output_balance - dest->amount;
      }
      out.balances = balance_list_new();
      balance_list_push(out.balances, &balance);

      // add to transaction output list
      tx_outputs_push(outputs, &out);

      balance_list_free(out.balances);
    }
  }

  if (!recv_eq_remainder) {
    // add unspent to output
    // creates output with dest address
    tx_output_t out = {};
    balance_t bal = {};
    memcpy(out.address, dest->receiver, TANGLE_ADDRESS_BYTES);
    memcpy(bal.color, dest->color, BALANCE_COLOR_BYTES);
    bal.value = dest->amount;
    out.balances = balance_list_new();
    balance_list_push(out.balances, &bal);
    tx_outputs_push(outputs, &out);
    balance_list_free(out.balances);
  }

  return outputs;
}

static int wallet_sign_tx(wallet_t* w, transaction_t* tx, unspent_outputs_t* inputs) {
  if (tx == NULL || tx->outputs == NULL || tx->inputs == NULL) {
    printf("[%s:%d] null parameters\n", __func__, __LINE__);
    return -1;
  }

  // calculate essence of the transaction
  byte_buf_t* essence = tx_essence(tx);
  if (essence == NULL) {
    printf("[%s:%d] transaction essence calculation failed\n", __func__, __LINE__);
    return -1;
  }

  // get signature
  byte_t addr_pub[ED_PUBLIC_KEY_BYTES] = {};
  byte_t addr_priv[ED_PRIVATE_KEY_BYTES] = {};
  byte_t addr_sig[ED_SIGNATURE_BYTES] = {};
  tx_output_t* out = NULL;

  if (tx->signatures) {
    ed_signatures_destory(&tx->signatures);
  }
  tx->signatures = ed_signatures_init();

  unspent_outputs_t *in, *in_tmp;
  HASH_ITER(hh, inputs, in, in_tmp) {
    sign_signature(w->addr_manager->seed, in->addr_index, essence->data, essence->len, addr_sig);
    address_ed25519_keypair(w->addr_manager->seed, in->addr_index, addr_pub, addr_priv);
    ed_signatures_add(&tx->signatures, in->addr, addr_pub, addr_sig);
  }

  byte_buf_free(essence);
  return 0;
}

wallet_t* wallet_init(char const url[], uint16_t port, byte_t const seed[], uint64_t last_addr, uint64_t first_unspent,
                      uint64_t last_unspent) {
  wallet_t* ctx = malloc(sizeof(wallet_t));
  if (ctx == NULL) {
    printf("[%s %d] OOM\n", __func__, __LINE__);
    return NULL;
  }

  // address manager, we should update address status later.
  // TODO: init local unspent/spent addresses
  bitmask_t* addr_mask = bitmask_new();
  if (addr_mask == NULL) {
    printf("[%s %d] create bitmask failed\n", __func__, __LINE__);
    goto err;
  }

  // mask spent address
  for (uint64_t i = 0; i < first_unspent; i++) {
    bitmask_op(addr_mask, i, BITMASK_SET);
  }

  ctx->addr_manager = am_new(seed, last_addr, addr_mask);
  if (ctx->addr_manager == NULL) {
    printf("[%s %d] create address manager failed\n", __func__, __LINE__);
    goto err;
  }
  ctx->addr_manager->first_unspent_idx = first_unspent;
  ctx->addr_manager->last_unspent_idx = last_unspent;
  ctx->addr_manager->last_addr_index = last_addr;

  // client endpoint
  strcpy(ctx->endpoint.url, url);
  ctx->endpoint.port = port;

  // init unspent output manager
  ctx->unspent = unspent_outputs_init();
  for (uint64_t i = 0; i <= last_addr; i++) {
    address_t tmp_addr = {};
    address_get(seed, i, ADDRESS_VER_ED25519, tmp_addr.addr);
    unspent_outputs_add(&ctx->unspent, tmp_addr.addr, i, NULL);
    if (i < first_unspent) {
      unspent_outputs_set_spent(&ctx->unspent, tmp_addr.addr, true);
    }
  }

  // fetch remote status, sync with node
  if (wallet_refresh(ctx, true) == false) {
    printf("[%s:%d] wallet status update failed\n", __func__, __LINE__);
  }

  if (addr_mask) {
    bitmask_free(addr_mask);
  }

  return ctx;

err:
  if (addr_mask) {
    bitmask_free(addr_mask);
  }

  wallet_free(ctx);
  return NULL;
}

void wallet_free(wallet_t* w) {
  if (w) {
    if (w->addr_manager) {
      am_free(w->addr_manager);
    }
    if (w->unspent) {
      unspent_outputs_free(&w->unspent);
    }
  }
  free(w);
}

bool wallet_is_node_synced(wallet_t* w) {
  if (!w) {
    printf("[%s:%d] wallet instance is uninitialized\n", __func__, __LINE__);
    return false;
  }

  res_node_info_t info;
  if (get_node_info(&w->endpoint, &info) != 0) {
    printf("[%s:%d] get node info failed\n", __func__, __LINE__);
    return false;
  }
  return info.is_synced;
}

void wallet_receive_address(wallet_t* w, byte_t addr[]) { am_get_last_unspent_address(w->addr_manager, addr); }

void wallet_new_receive_address(wallet_t* w, byte_t addr[]) { am_get_new_address(w->addr_manager, addr); }

void wallet_remainder_address(wallet_t* w, byte_t addr[]) { am_get_first_unspent_address(w->addr_manager, addr); }

uint64_t wallet_remainder_address_index(wallet_t* w) { return w->addr_manager->first_unspent_idx; }

addr_list_t* wallet_addresses(wallet_t* w) { return am_addresses(w->addr_manager); }

addr_list_t* wallet_unspent_addresses(wallet_t* w) { return am_unspent_addresses(w->addr_manager); }

addr_list_t* wallet_spent_addresses(wallet_t* const w) { return am_spent_addresses(w->addr_manager); }

bool wallet_refresh(wallet_t* w, bool include_spent) {
  bool ret = true;
  addr_list_t* addrs = NULL;
  bool has_ids = false;
  if (include_spent) {
    addrs = wallet_addresses(w);
  } else {
    addrs = wallet_unspent_addresses(w);
  }

  if (addrs == NULL || addr_list_len(addrs) == 0) {
    printf("[%s:%d] empty address list\n", __func__, __LINE__);
    ret = false;
    goto end;
  }

  unspent_outputs_t* res = unspent_outputs_init();

  if (get_unspent_outputs(&w->endpoint, addrs, &res) == 0) {
    unspent_outputs_t *unspent, *tmp;
    HASH_ITER(hh, res, unspent, tmp) {
      // get the local status of this address
      unspent_outputs_t* elm = unspent_outputs_find(&w->unspent, unspent->addr);
      bool is_spent = false;
      if (elm) {
        // restore the spent status
        is_spent = elm->spent;
        unspent_outputs_update(&w->unspent, unspent->addr, unspent->ids);
        // mark the output as spent if we already marked it as spent locally
        unspent_outputs_set_spent(&w->unspent, unspent->addr, is_spent);
      } else {
        // TODO: we don't know the address index from unspent outputs API response
        unspent_outputs_add(&w->unspent, unspent->addr, unspent->addr_index, unspent->ids);
      }
    }
  }

end:
  addr_list_free(addrs);
  unspent_outputs_free(&res);

  return ret;
}

uint64_t wallet_balance(wallet_t* w) {
  wallet_refresh(w, false);
  return unspent_outputs_balance(&w->unspent);
}

int wallet_request_funds(wallet_t* w) {
  int ret = -1;
  byte_t receiver[TANGLE_ADDRESS_BYTES];
  res_get_funds_t res = {};
  wallet_receive_address(w, receiver);
  ret = get_funds(&w->endpoint, receiver, &res);
  printf("[%s:%d] message ID: %s\n", __func__, __LINE__, res.msg_id);
  return ret;
}

int wallet_send_funds(wallet_t* w, send_funds_op_t* dest) {
  int ret = 0;
  transaction_t tx = {};

  // validating send funds options
  if (dest->amount <= 0 || empty_byte_array(dest->receiver, TANGLE_ADDRESS_BYTES)) {
    printf("[%s:%d] Invalid amount or receiver address\n", __func__, __LINE__);
    return -1;
  }

  // sync with node before sending
  wallet_refresh(w, false);

  // looking for request founds in current unspent outputs
  unspent_outputs_t* consumed_outputs = unspent_outputs_required_outputs(&w->unspent, dest->amount, dest->color);
  if (!consumed_outputs) {
    printf("[%s:%d] error on finding outputs\n", __func__, __LINE__);
    return -1;
  }

  // is the balance enough?
  uint64_t output_balance = unspent_outputs_balance_with_color(&consumed_outputs, dest->color);
  if (output_balance < dest->amount) {
    printf("[%s:%d] Insufficient balance (balance %" PRIu64 " < required %" PRIu64 ")\n", __func__, __LINE__,
           output_balance, dest->amount);
    ret = -1;
    goto end;
  }

  // build transaction
  // transaction inputs
  tx.inputs = wallet_build_inputs(w, consumed_outputs);

  // transaction outputs
  tx.outputs = wallet_build_outputs(w, dest, consumed_outputs);

  // sign transaction
  wallet_sign_tx(w, &tx, consumed_outputs);

  // validate tx
  if (tx_signautres_valid(&tx) == false) {
    printf("[%s:%d] transaction validation failed\n", __func__, __LINE__);
    ret = -1;
    goto end;

  } else {
    res_send_tx_t res = {};
    byte_buf_t* tx_bytes = tx_2_base64(&tx);
    if (send_tx_bytes(&w->endpoint, tx_bytes->data, &res) == 0) {
      // success
      printf("[%s:%d] message ID: %s\n", __func__, __LINE__, res.msg_id);
    } else {
      printf("[%s:%d send transaction failed\n", __func__, __LINE__);
    }
    byte_buf_free(tx_bytes);
  }

  // mark address as spent if transaction sent successfully
  unspent_outputs_t *output_sent, *tmp_sent;
  HASH_ITER(hh, consumed_outputs, output_sent, tmp_sent) {
    unspent_outputs_set_spent(&w->unspent, output_sent->addr, true);
    am_mark_spent_address(w->addr_manager, output_sent->addr_index);
  }

end:
  // clean up
  if (tx.inputs) {
    tx_inputs_free(tx.inputs);
  }
  if (tx.outputs) {
    tx_outputs_free(tx.outputs);
  }
  if (tx.signatures) {
    ed_signatures_destory(&tx.signatures);
  }
  unspent_outputs_free(&consumed_outputs);
  return ret;
}

void wallet_status_print(wallet_t* w) {
  printf("========= Wallet Status =========\n");
  am_print(w->addr_manager);
  printf("========= outputs =========\n");
  unspent_outputs_print(&w->unspent);
}
