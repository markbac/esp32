/**
 * @file    web_api.h
 * @brief   JSON data API for the native web dashboard.
 *
 * @details Endpoints (all GET, read-only except that lookups queue fetches):
 *          | Path                      | Content                                                      |
 *          |---------------------------|--------------------------------------------------------------|
 *          | `/api/state.json`         | Everything the device knows, ~20 KB (see web_api.cpp)        |
 *          | `/api/photo?hex=4ac9e7`   | Cached aircraft photo (JPEG); 202 while being fetched        |
 *          | `/api/lookup?hex=&cs=`    | Queue a route and photo lookup for an aircraft (202)         |
 *
 *          Secrets never appear in any response.
 */
#pragma once

class WebServer;

namespace WebApi {

/** @brief Register the API routes on @p server. */
void registerRoutes(WebServer &server, const char *boardName);

}  // namespace WebApi
