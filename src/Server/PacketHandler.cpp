#include <BaseApp.h> // precompiled
#include <Server/PacketHandler.h>

#include <Client/GameClient.h>

// text packets
#include <Packet/Client/LogonPacketListener.h>
// generic action packets
#include <Packet/Client/Generic/EnterGameListener.h>
// tank update packets


PacketHandler g_handler;
PacketHandler* GetPacketHandler() { return &g_handler; }

PacketHandler::PacketHandler(ENetHost* pHost)
{
	m_pHost = pHost;
}

void PacketHandler::HandleIncomingClientPacket(ENetPeer* pConnectionPeer, ENetPacket* pPacket)
{
	if (pConnectionPeer == NULL || pPacket == NULL || pPacket->dataLength < 4)
	{
		// connection peer may be null
		// packet may be null
		// packet may not have the message type header inside it

		// in this case we cannot procceed handling the incoming packet
		LogError("failed to proccess incoming packet, something went wrong!");
		return;
	}

	// message type is the header of the packet the growtopia client sends us, without it, we cannot define what packet is the client trying to send
	int msgType = *(int*)pPacket->data;
	switch (msgType)
	{
		case NET_MESSAGE_GENERIC_TEXT:
		{
		    // the genertic text message type is a text packet the client sends
		    // most common action text packets sent from the client use this msg type
		    // such as: action|enter_game; action|refresh_items_data; action|drop; action|trash; action|growid; etc...
			if (pConnectionPeer->data == NULL || pPacket->dataLength > 1024)
			{
				// game client may be null
				// packet may exceed the lengths we support

				// in this case we cannot procceed handling the incoming packet
				LogError("failed to proccess incoming NET_MESSAGE_GENERIC_TEXT packet, something went wrong!(2)");
				return;
			}

			memset(pPacket->data + pPacket->dataLength - 1, 0, 1);
			// this is the text packet data the client sends
			nova_str textPacket = (const char*)pPacket->data + 4;
			GameClient *pClient = (GameClient*)pConnectionPeer->data;
			if (pClient == NULL)
			{
				// game client is null
				LogError("failed to proccess incoming NET_MESSAGE_GENERIC_TEXT packet, client is NULL!");
				return;
			}

			printf("%s\n", textPacket.c_str());
			if (textPacket.starts_with("requestedName"))
			{
				// this is the text packet guest(non-registered) accounts send when logging in
				GrowPacketsListener::OnHandleGuestLogon(pClient, textPacket);
				return;
			}

			if (textPacket.starts_with("tankIDName"))
			{
				// this is the text packet registered accounts send when logging in
				GrowPacketsListener::OnHandleGrowIDLogon(pClient, textPacket);
				return;
			}

			if (textPacket.starts_with("protocol|") && textPacket.find("ltoken|") != std::string::npos)
			{
				// in 4.61 version, ubisoft decided to make token-based login system to support external logins such as steam, google
				// the ltoken is the login token containing the login packet info
				// ^ the token is cyphered with base64, so to get the login packet info we must decypher it

				GrowPacketsListener::OnHandleTokenLogon(pClient, textPacket);
				return;
			}

			if (textPacket.starts_with("action|refresh_item_data"))
			{
				if (pClient == NULL)
				{
					// failed to procceed to update items data because client is null
					return;
				}

				// updating items data packet, without it, client would get cursed textures inside world because it haven't gotten the items data correctly
				GameUpdatePacket *pRefreshItemsPacket = GetItemInfoManager()->GetUpdatePacket();
				if (pRefreshItemsPacket == NULL)
				{
					// the packet was null(fail), so we cannot really procceed updating data...
					pClient->SendLog("Something went wrong trying to update the items data.");
					LogError("ItemInfoManager >> Error! Seems like refreshing packet is null, check it out ASAP!");
					return;
				}

				// sending the packet to update the items
				pClient->SendPacketRaw(NET_MESSAGE_GAME_PACKET, pRefreshItemsPacket, GUP_SIZE + pRefreshItemsPacket->dataLength, ENET_PACKET_FLAG_RELIABLE);
				return;
			}

			if (textPacket.starts_with("action|enter_game"))
			{
				GrowPacketsListener::OnHandleGameEnter(pClient);
				return;
			}

			break;
		}

		case NET_MESSAGE_GAME_MESSAGE:
		{
			// the genertic text message type is a text packet the client sends
			// most common action text packets sent from the client use this msg type
			
			// actions using this message type:
			// action|quit
			// action|world_button
			// action|validate_world
			// action|join_request
			// action|quit_to_exit
			// action|gohomeworld
			if (pConnectionPeer->data == NULL || pPacket->dataLength > 1024)
			{
				// game client may be null
				// packet may exceed the lengths we support

				// in this case we cannot procceed handling the incoming packet
				LogError("failed to proccess incoming NET_MESSAGE_GAME_MESSAGE packet, something went wrong!(2)");
				return;
			}

			memset(pPacket->data + pPacket->dataLength - 1, 0, 1);
			// this is the text packet data the client sends
			nova_str textPacket = (const char*)pPacket->data + 4;
			GameClient* pClient = (GameClient*)pConnectionPeer->data;
			if (pClient == NULL)
			{
				// game client is null
				LogError("failed to proccess incoming NET_MESSAGE_GAME_MESSAGE packet, client is NULL!");
				return;
			}

			printf("%s\n", textPacket.c_str());
			if (textPacket == "action|quit")
			{
				// client disconnects from the server instantly
				enet_peer_disconnect_later(pConnectionPeer, 0U);
				return;
			}

			if (textPacket.starts_with("action|world_button"))
			{
				// world categories in main menu
				// in 3.xx, ubisoft changed the menu, fortunarely for us, we will handle both of them based on client's version :)
				return;
			}

			break;
		}

		case NET_MESSAGE_GAME_PACKET:
		{
			if (pConnectionPeer->data == NULL || pPacket->dataLength < 60 || pPacket->dataLength > 61)
			{
				// game client may be null
				// packet may not have the lengths we support

				// in this case we cannot procceed handling the incoming packet
				LogError("failed to proccess incoming NET_MESSAGE_GAME_PACKET packet, something went wrong!(2)");
				return;
			}

			GameClient* pClient = (GameClient*)pConnectionPeer->data;
			if (pClient == NULL)
			{
				// game client is null
				LogError("failed to proccess incoming NET_MESSAGE_GAME_PACKET packet, client is NULL!");
				return;
			}

			// this message type is more complex, it contains it's own structure, original variables's name is unknown without access to the source code or reverse engineering it from the client
			// we managed to retrieve some of them, others are named after bruteforcing them manually
			// the structure has default size of 56 - sizeof(gameupdatepacket_t)
			// there is cases where it has larger size, if in the flags column of it, has the flag NET_GAME_PACKET_FLAG_EXTENDED
			// the flag mentioned above allows the client to read tankpacket's data column, whereas dataLength column contains the length of the "extended data" written
			gameupdatepacket_t* pTankPacket = reinterpret_cast<gameupdatepacket_t*>(pPacket->data + 4);
			if (pTankPacket == NULL)
			{
				// tank packet is null
				LogError("failed to proccess incoming NET_MESSAGE_GAME_PACKET packet, tank packet is NULL!");
				return;
			}

			// tank packets are handled by their type, they are explained below
			switch (pTankPacket->type)
			{
				case NET_GAME_PACKET_STATE:
				{
			        // this packet is what client sends when he is inside world, moving(walking)
				    // we do not have worlds yet, so cannot handle
					break;
			    }
			}

			break;
		}
	}
}