"use strict";
// SPDX-License-Identifier: GPL-3.0-or-later
// myMPD (c) 2018-2023 Juergen Mang <mail@jcgames.de>
// https://github.com/jcorporation/mympd

/** @module modalPlaylistCopy_js */

/**
 * Shows the copy playlist modal
 * @param {Array} srcPlists playlist to remove the entries
 * @returns {void}
 */
function showCopyPlaylist(srcPlists) {
    const modal = document.getElementById('modalPlaylistCopy');
    cleanupModal(modal);
    setData(modal, 'srcPlists', srcPlists);
    filterPlaylistsSelect(1, 'copyPlaylistPlaylist', '', '');
    uiElements.modalPlaylistCopy.show();
}

/**
 * Copies the playlist to another playlist
 * @returns {void}
 */
//eslint-disable-next-line no-unused-vars
function copyPlaylist() {
    const modal = document.getElementById('modalPlaylistCopy');
    cleanupModal(modal);
    const srcPlists = getData(modal, 'srcPlists');
    const mode = getRadioBoxValueId('copyPlaylistMode');
    const plistEl = document.getElementById('copyPlaylistPlaylist');
    if (validatePlistEl(plistEl) === false) {
        return;
    }
    sendAPI("MYMPD_API_PLAYLIST_COPY", {
        "srcPlists": srcPlists,
        "dstPlist": plistEl.value,
        "mode": Number(mode)
    }, copyPlaylistClose, true);
}

/**
 * Handles the response of "copy playlist" modal
 * @param {object} obj jsonrpc response
 * @returns {void}
 */
function copyPlaylistClose(obj) {
    if (obj.error) {
        showModalAlert(obj);
    }
    else {
        uiElements.modalPlaylistCopy.hide();
    }
}
