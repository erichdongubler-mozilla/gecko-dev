/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.DownloadAction
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.downloads.FileSizeFormatter
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import org.mozilla.fenix.downloads.listscreen.store.CreatedTime
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIAction
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState
import org.mozilla.fenix.downloads.listscreen.store.FileItem
import org.mozilla.fenix.ext.getBaseDomainUrl
import java.time.Instant

/**
 * Middleware for loading and mapping download items from the browser store.
 *
 * @param browserStore [BrowserStore] instance to get the download items from.
 * @param fileSizeFormatter [FileSizeFormatter] used to format the size of the file item.
 * @param scope The [CoroutineScope] that will be used to launch coroutines.
 * @param dateTimeProvider The [DateTimeProvider] that will be used to get the current date.
 */
class DownloadUIMapperMiddleware(
    private val browserStore: BrowserStore,
    private val fileSizeFormatter: FileSizeFormatter,
    private val scope: CoroutineScope,
    private val dateTimeProvider: DateTimeProvider = DateTimeProviderImpl(),
) : Middleware<DownloadUIState, DownloadUIAction> {

    override fun invoke(
        context: MiddlewareContext<DownloadUIState, DownloadUIAction>,
        next: (DownloadUIAction) -> Unit,
        action: DownloadUIAction,
    ) {
        next(action)
        when (action) {
            is DownloadUIAction.Init -> {
                browserStore.dispatch(DownloadAction.RemoveDeletedDownloads)
                update(context.store)
            }

            else -> {
                // no - op
            }
        }
    }

    private fun update(store: Store<DownloadUIState, DownloadUIAction>) {
        scope.launch {
            browserStore.flow()
                .distinctUntilChangedBy { it.downloads }
                .map { it.downloads.toFileItemsList() }
                .collect {
                    store.dispatch(DownloadUIAction.UpdateFileItems(it))
                }
        }
    }

    private fun Map<String, DownloadState>.toFileItemsList(): List<FileItem> =
        values
            .distinctBy { it.fileName }
            .sortedByDescending { it.createdTime } // sort from newest to oldest
            .map { it.toFileItem() }
            .filter { it.status == DownloadState.Status.COMPLETED }

    private fun DownloadState.toFileItem() =
        FileItem(
            id = id,
            url = url,
            fileName = fileName,
            filePath = filePath,
            formattedSize = fileSizeFormatter.formatSizeInBytes(contentLength ?: 0),
            displayedShortUrl = url.getBaseDomainUrl(),
            contentType = contentType,
            status = status,
            createdTime = categorizeTime(createdTime),
        )

    private fun categorizeTime(epochMillis: Long): CreatedTime {
        val currentDate = dateTimeProvider.currentLocalDate()
        val inputDate = Instant.ofEpochMilli(epochMillis)
            .atZone(dateTimeProvider.currentZoneId())
            .toLocalDate()

        return when {
            inputDate.isEqual(currentDate) -> CreatedTime.TODAY
            inputDate.isEqual(currentDate.minusDays(1)) -> CreatedTime.YESTERDAY
            inputDate.isAfter(currentDate.minusDays(NUM_DAYS_IN_LAST_7_DAYS_PERIOD)) -> CreatedTime.LAST_7_DAYS
            inputDate.isAfter(currentDate.minusDays(NUM_DAYS_IN_LAST_30_DAYS_PERIOD)) -> CreatedTime.LAST_30_DAYS
            else -> CreatedTime.OLDER
        }
    }

    /**
     * Constants for [DownloadUIMapperMiddleware].
     */
    companion object {
        private const val NUM_DAYS_IN_LAST_7_DAYS_PERIOD = 7L
        private const val NUM_DAYS_IN_LAST_30_DAYS_PERIOD = 30L
    }
}
