#ifndef __WALLET_API_H__
#define __WALLET_API_H__

#include <stdbool.h>

#include "client/client_service.h"
#include "core/unspent_outputs.h"
#include "wallet/address_manager.h"
#include "wallet/asset_registry.h"
typedef struct {
  tangle_client_conf_t endpoint;
  wallet_am_t* addr_manager;
  unspent_outputs_t* unspent;  // unspent outputs
  // wallet_ar_t asset_reg;
} wallet_t;

// a struct that is used to aggregate the optional parameters provided in the send founds call
typedef struct {
  uint64_t amount;
  byte_t receiver[TANGLE_ADDRESS_BYTES];
  byte_t color[BALANCE_COLOR_BYTES];
  byte_t remainder[TANGLE_ADDRESS_BYTES];
} send_funds_op_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates or initializes a wallet instance
 *
 * @param[in] url The URL of an endpoint
 * @param[in] port The port number, 0 for default port (8443 or 443)
 * @param[in] seed The seed, NULL for random seed
 * @param[in] last_addr The last address index
 * @param[in] first_unspent The first unspent address index
 * @param[in] last_unspent The last unspent address index
 * @return wallet_t* A wallet instance
 */
wallet_t* wallet_init(char const url[], uint16_t port, byte_t const seed[], uint64_t last_addr, uint64_t first_unspent,
                      uint64_t last_unspent);

/**
 * @brief Refresh wallet status with node
 *
 * @param[in] w A wallet instance
 * @param[out] include_spent False for unspent address only
 * @return true On success
 * @return false On failed
 */
bool wallet_refresh(wallet_t* w, bool include_spent);

/**
 * @brief Frees a wallet instance
 *
 * @param[in] w A wallet instance
 */
void wallet_free(wallet_t* w);

/**
 * @brief Checks if connected endpoint is synced.
 *
 * @param[in] w A wallet instance
 * @return true The connected node is synced
 * @return false The connected node is unsynced
 */
bool wallet_is_node_synced(wallet_t* w);

/**
 * @brief Gets a receive address. aka the last unspent address
 *
 * @param[in] w A wallet instance
 * @param[out] addr The receive address
 */
void wallet_receive_address(wallet_t* w, byte_t addr[]);

/**
 * @brief Generates and returns a new unused receive address.
 *
 * @param[in] w A wallet instance
 * @param[out] addr The new unused address
 */
void wallet_new_receive_address(wallet_t* w, byte_t addr[]);

/**
 * @brief Returns the address that is used for the remainder of funds, aka first unspent address
 *
 * @param[in] w A wallet instance
 * @param[out] addr The remainder address
 */
void wallet_remainder_address(wallet_t* w, byte_t addr[]);

/**
 * @brief Gets a list of all addresses of the wallet.
 *
 * @param[in] w A wallet instance
 * @return addr_list_t*
 */
addr_list_t* wallet_addresses(wallet_t* const w);

/**
 * @brief Gets a list of all unspent addresses of the wallet.
 *
 * @param[in] w A wallet instance
 * @return addr_list_t*
 */
addr_list_t* wallet_unspent_addresses(wallet_t* const w);

/**
 * @brief Gets a list of all spent addresses of the wallet.
 *
 * @param[in] w A wallet instance
 * @return addr_list_t*
 */
addr_list_t* wallet_spent_addresses(wallet_t* const w);

/**
 * @brief Requests some funds from the faucet for testing purpose
 *
 * @param[in] w A wallet instance
 * @return int 0 on success
 */
int wallet_request_funds(wallet_t* w);

/**
 * @brief the confirmed balance of the funds managed by this wallet.
 *
 * @param[in] w A wallet instance
 * @return uint64_t The sum of confirmed balances
 */
uint64_t wallet_balance(wallet_t* w);

/**
 * @brief Prints out local wallet status
 *
 * @param[in] w A wallet instance
 */
void wallet_status_print(wallet_t* w);

/**
 * @brief Issues a payment of the given option
 *
 * @param[in] w A wallet instance
 * @param[in] dest A send funds option
 * @return int 0 on success
 */
int wallet_send_funds(wallet_t* w, send_funds_op_t* dest);

// ========= TODO =========

// creates a new colored token with the given details.
int wallet_create_asset();
int wallet_update_asset();
int wallet_delete_asset();

#ifdef __cplusplus
}
#endif

#endif