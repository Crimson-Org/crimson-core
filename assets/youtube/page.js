// Runs inside a YouTube page that a Crimson app drives, and gives the app's own page script the shared pieces as
// window.crimsonPage: the bridge to the app, YouTube's own API, comments, the player and the video-only view
(() => {
	const READY_CHECK_MILLISECONDS = 100
	const WATCH_MILLISECONDS = 250
	const PLAYING = 1
	const PAUSED = 2
	const BUFFERING = 3
	// A video this close to the start counts as starting over
	const START_SECONDS = 1
	const YOUTUBE_CLIENT = {clientName: 'WEB', clientVersion: '2.20250101.00.00'}
	const VIDEO_CLASS = 'crimson-video'
	const VIDEO_STYLE_ID = 'crimson-video-style'
	const VIDEO_PATH_CLASS = 'crimson-video-path'
	const VIDEO_SETTLE_MS = 1500
	// The player is lifted out of the page's layout to cover all of it, which also makes YouTube pick a sharper stream,
	// and YouTube's controls, suggestion cards, annotations and end screens are hidden over the video
	const VIDEO_STYLE = `
		html.crimson-video, html.crimson-video body { background: #000 !important; overflow: hidden !important; }
		html.crimson-video body * { visibility: hidden !important; }
		html.crimson-video #movie_player, html.crimson-video #movie_player * { visibility: visible !important; }
		html.crimson-video .crimson-video-path, html.crimson-video #movie_player, html.crimson-video #movie_player video {
			transform: none !important; filter: none !important; contain: none !important; will-change: auto !important;
			opacity: 1 !important; transition: none !important; animation: none !important;
		}
		html.crimson-video .crimson-video-path { z-index: 2147483647 !important; }
		html.crimson-video #movie_player {
			position: fixed !important; inset: 0 !important; width: 100vw !important; height: 100vh !important;
			z-index: 2147483647 !important; background: #000 !important; transform: none !important;
			visibility: visible !important;
		}
		html.crimson-video #movie_player .html5-video-container, html.crimson-video #movie_player video {
			position: absolute !important; left: 0 !important; top: 0 !important; width: 100vw !important;
			height: 100vh !important; margin: 0 !important; object-fit: contain !important;
		}
		html.crimson-video #movie_player .ytp-chrome-top, html.crimson-video #movie_player .ytp-chrome-bottom,
		html.crimson-video #movie_player .ytp-gradient-top, html.crimson-video #movie_player .ytp-gradient-bottom,
		html.crimson-video #movie_player .ytp-pause-overlay, html.crimson-video #movie_player .ytp-ce-element,
		html.crimson-video #movie_player .ytp-cards-teaser, html.crimson-video #movie_player .ytp-cards-button,
		html.crimson-video #movie_player .ytp-suggested-action, html.crimson-video #movie_player .ytp-paid-content-overlay,
		html.crimson-video #movie_player .iv-branding, html.crimson-video #movie_player .annotation,
		html.crimson-video #movie_player .video-annotations, html.crimson-video #movie_player .html5-endscreen,
		html.crimson-video #movie_player .ytp-endscreen-content, html.crimson-video #movie_player .ytp-autonav-endscreen,
		html.crimson-video #movie_player .ytp-overlays-container, html.crimson-video #movie_player .ytp-player-content {
			display: none !important;
		}`

	const send = message => window.webkit.messageHandlers.crimson.postMessage(message)
	const getPlayer = () => document.getElementById('movie_player')
	const readText = value => value?.runs?.map(run => run.text).join('') ?? value?.simpleText ?? ''


	function readCookie(name) {
		const prefix = `${name}=`
		const cookie = document.cookie.split('; ').find(entry => entry.startsWith(prefix))
		return cookie?.slice(prefix.length)
	}


	// Depth first search for the first value stored under the key, since YouTube nests renderers differently per page
	function findKey(value, key) {
		if (!value || typeof value !== 'object')
			return undefined
		if (key in value)
			return value[key]
		for (const child of Object.values(value)) {
			const found = findKey(child, key)
			if (found !== undefined)
				return found
		}
		return undefined
	}


	function findAll(value, key, found = []) {
		if (!value || typeof value !== 'object')
			return found
		if (key in value)
			found.push(value[key])
		Object.values(value).forEach(child => findAll(child, key, found))
		return found
	}


	async function hashText(text) {
		const digest = await crypto.subtle.digest('SHA-1', new TextEncoder().encode(text))
		return [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, '0')).join('')
	}


	// The page's own request signature, which YouTube asks for on every signed in call
	async function createHeaders() {
		const headers = {
			'Content-Type': 'application/json',
			'X-Origin': location.origin,
			'X-Goog-AuthUser': String(ytcfg.get('SESSION_INDEX') ?? 0),
		}
		const visitor = ytcfg.get('VISITOR_DATA')
		if (visitor)
			headers['X-Goog-Visitor-Id'] = visitor
		const secret = readCookie('SAPISID') ?? readCookie('__Secure-3PAPISID')
		if (secret) {
			const now = Math.floor(Date.now() / 1000)
			headers.Authorization = `SAPISIDHASH ${now}_${await hashText(`${now} ${secret} ${location.origin}`)}`
		}
		return headers
	}


	async function post(endpoint, query, body) {
		const response = await fetch(`/youtubei/v1/${endpoint}?prettyPrint=false${query}`, {
			method: 'POST',
			credentials: 'include',
			headers: await createHeaders(),
			body: JSON.stringify(body),
		})
		if (!response.ok)
			throw new Error(`YouTube answered ${response.status}`)
		return response.json()
	}


	// Calls the page's own client, where older lists continue through URL parameters and newer ones through the body
	async function callApi(endpoint, body, continuation) {
		const query = continuation?.isLegacy
			? `&ctoken=${continuation.token}&continuation=${continuation.token}&type=next`
			: ''
		if (continuation && !continuation.isLegacy)
			body = {continuation: continuation.token}
		return post(endpoint, query, {context: ytcfg.get('INNERTUBE_CONTEXT'), ...body})
	}


	// Comments and the full search are YouTube's own, so they come from YouTube's own web client whatever site the
	// page is on, which needs no sign in, and comments come in the order YouTube's top comments show
	async function callYouTube(endpoint, body) {
		const context = ytcfg.get('INNERTUBE_CONTEXT')
		return post(endpoint, '', {context: {...context, client: {...context.client, ...YOUTUBE_CLIENT}}, ...body})
	}


	function findContinuation(...places) {
		for (const place of places) {
			const legacy = findKey(place, 'nextContinuationData')?.continuation
			if (legacy)
				return {token: legacy, isLegacy: true}
			const token = findKey(place, 'continuationCommand')?.token
			if (token)
				return {token, isLegacy: false}
		}
		return undefined
	}


	function readCommentPage(answer) {
		const items = (answer.onResponseReceivedEndpoints ?? []).flatMap(endpoint =>
			(endpoint.reloadContinuationItemsCommand ?? endpoint.appendContinuationItemsAction)?.continuationItems ?? [])
		const payloads = new Map((answer.frameworkUpdates?.entityBatchUpdate?.mutations ?? [])
			.filter(mutation => mutation.payload?.commentEntityPayload)
			.map(mutation => [mutation.entityKey, mutation.payload.commentEntityPayload]))
		// Top comments come in threads that carry where their replies start, and replies come on their own
		const comments = items.map(item => {
			const thread = item.commentThreadRenderer
			const comment = payloads.get(thread?.commentViewModel?.commentViewModel?.commentKey
				?? item.commentViewModel?.commentKey)
			return comment && {
				author: comment.author?.displayName ?? '',
				text: comment.properties?.content?.content ?? '',
				published: comment.properties?.publishedTime ?? '',
				likes: comment.toolbar?.likeCountNotliked?.trim() ?? '',
				replies: comment.toolbar?.replyCount ? String(comment.toolbar.replyCount) : '',
				repliesToken: findKey(thread?.replies, 'token') ?? '',
			}
		}).filter(Boolean)
		const next = items.at(-1)?.continuationItemRenderer
		return {comments, next: findKey(next, 'token') ?? ''}
	}


	// The first call finds where a video's comments start, and each later one carries on from the last
	async function loadComments(video, token) {
		let page = {comments: [], next: ''}
		try {
			if (!token) {
				const watch = await callYouTube('next', {videoId: video})
				const section = findAll(watch, 'itemSectionRenderer')
					.find(item => item.sectionIdentifier === 'comment-item-section')
				token = findKey(section, 'token')
			}
			if (token) {
				const answer = await callYouTube('next', {continuation: token})
				page = readCommentPage(answer)
			}
		} catch (error) {
			send({type: 'warning', message: String(error)})
		}
		send({type: 'comments', video, comments: page.comments, next: page.next})
	}


	// A thread's replies come a few at a time, each answer saying where the next ones start
	async function loadReplies(video, token, id) {
		let page = {comments: [], next: ''}
		try {
			page = readCommentPage(await callYouTube('next', {continuation: token}))
		} catch (error) {
			send({type: 'warning', message: String(error)})
		}
		send({type: 'replies', video, id, comments: page.comments, next: page.next})
	}


	// Whether the player has started listening to its video element, which YouTube keeps between videos
	let isListening = false
	async function waitForPlayer() {
		while (!getPlayer()?.loadVideoById)
			await new Promise(resolve => setTimeout(resolve, READY_CHECK_MILLISECONDS))
		const media = getPlayer().querySelector('video')
		if (media && !isListening) {
			isListening = true
			for (const event of ['playing', 'seeked', 'timeupdate'])
				media.addEventListener(event, watchPlayer)
		}
		return getPlayer()
	}


	// YouTube resumes long videos where they were last stopped, so a video that should start over plays at no
	// volume until it is back at the start, since the player will not start a muted video at all
	let startingOver = ''
	let hasSoughtStart = false
	// The volume the app asked for, which the page puts back whenever YouTube applies its own remembered one
	let wantedVolume = -1
	// Whether the last pause came from the app, so a pause YouTube or WebKit makes alone is logged
	let isPausing = false
	let lastState = -1


	// Loading alone leaves the video unstarted when the page was paused, so it is told to play as well
	async function play(id, volume, resume) {
		const player = await waitForPlayer()
		startingOver = resume ? '' : id
		hasSoughtStart = false
		isPausing = false
		wantedVolume = volume
		player.unMute()
		player.loadVideoById(id)
		player.setVolume(resume ? volume : 0)
		player.playVideo()
	}


	// Only a playing video counts, since while it buffers it still sits at the start before YouTube jumps ahead
	function startOver(player, video, state, time) {
		if (video !== startingOver)
			return
		// Loading a video brings back YouTube's own volume, so it is held at none on every check
		player.setVolume(0)
		if (state !== PLAYING)
			return
		if (time > START_SECONDS) {
			if (!hasSoughtStart)
				player.seekTo(0, true)
			hasSoughtStart = true
			return
		}
		startingOver = ''
		player.setVolume(wantedVolume)
	}


	function keepVolume(player) {
		if (startingOver || wantedVolume < 0 || player.getVolume?.() === wantedVolume)
			return
		player.setVolume(wantedVolume)
	}


	// The video fills the whole page, with YouTube's own controls hidden, while the app shows the page
	function showVideo(isShown) {
		if (!document.getElementById(VIDEO_STYLE_ID)) {
			const style = document.createElement('style')
			style.id = VIDEO_STYLE_ID
			style.textContent = VIDEO_STYLE
			document.head.append(style)
		}
		// The player sits inside YouTube's hidden, shifted player page, so every layer around it is let out too
		for (let node = getPlayer()?.parentElement; node && node !== document.body; node = node.parentElement)
			node.classList.toggle(VIDEO_PATH_CLASS, Boolean(isShown))
		document.documentElement.classList.toggle(VIDEO_CLASS, Boolean(isShown))
		window.dispatchEvent(new Event('resize'))
		// The app takes its cover away once the page has grown to its new size and the video spans it, with two more
		// frames drawn, or after a longer wait when that never comes
		return new Promise(resolve => {
			const started = performance.now()
			const check = () => {
				const video = getPlayer()?.querySelector('video')?.getBoundingClientRect()
				const spans = innerWidth > 1 && video && Math.abs(video.left) < 1 && Math.abs(video.top) < 1
					&& Math.abs(video.width - innerWidth) < 1 && Math.abs(video.height - innerHeight) < 1
				if (!isShown || performance.now() - started > VIDEO_SETTLE_MS)
					resolve()
				else if (spans)
					requestAnimationFrame(() => requestAnimationFrame(resolve))
				else
					requestAnimationFrame(check)
			}
			requestAnimationFrame(check)
			setTimeout(resolve, VIDEO_SETTLE_MS)
		})
	}


	let lastReport = ''


	// Reports the player only when something visible changed, which is every tick while a video plays, with what the
	// app's own script adds through describePlayer
	function watchPlayer() {
		const player = getPlayer()
		if (!player?.getPlayerState)
			return
		const state = player.getPlayerState()
		const video = player.getVideoData?.()?.video_id ?? ''
		const report = {
			type: 'player',
			video,
			state,
			time: player.getCurrentTime?.() ?? 0,
			duration: player.getDuration?.() ?? 0,
			...page.describePlayer?.(player, video),
		}
		// A video still on its way back to the start shows as loading at the start, so the app turns from spinner
		// to pause once, when the video really plays from the beginning
		if (report.video === startingOver) {
			report.time = 0
			report.state = BUFFERING
		}
		startOver(player, report.video, state, player.getCurrentTime?.() ?? 0)
		keepVolume(player)
		noticeLonePause(player, report.video, state)
		const key = JSON.stringify(report)
		if (key === lastReport)
			return
		lastReport = key
		send(report)
	}


	// Videos sometimes stop at the start after a long day of playing, and this tells which part paused them
	function noticeLonePause(player, video, state) {
		const previous = lastState
		lastState = state
		if (state !== PAUSED || previous === PAUSED || isPausing)
			return
		const media = player.querySelector('video')
		send({type: 'warning', message: `Video ${video} paused without being asked, at ${player.getCurrentTime?.()} s, `
			+ `after state ${previous}, starting over ${video === startingOver}, media paused ${media?.paused}, `
			+ `ready ${media?.readyState}, network ${media?.networkState}, error ${media?.error?.code}, `
			+ `idle ${Math.round((Date.now() - window._lact) / 1000)} s`})
	}


	const page = {
		send,
		getPlayer,
		readText,
		findKey,
		findAll,
		callApi,
		callYouTube,
		findContinuation,
		loadComments,
		loadReplies,
		showVideo,
		play,
		pause: () => {
			isPausing = true
			getPlayer()?.pauseVideo()
		},
		resume: () => {
			isPausing = false
			getPlayer()?.playVideo()
		},
		seek: seconds => getPlayer()?.seekTo(seconds, true),
		setVolume: volume => {
			wantedVolume = volume
			if (!startingOver)
				getPlayer()?.setVolume(volume)
		},
		// The app's own script sets this to add its own fields to each player report
		describePlayer: undefined,
	}
	window.crimsonPage = page


	// The desktop's media keys reach the page through WebKit's own media player, so its controls are handed to the
	// app, which keeps the queue and paces play and pause, including when YouTube sets its handlers again later
	const mediaSession = navigator.mediaSession
	if (mediaSession) {
		const APP_MEDIA_ACTIONS = {nexttrack: 'next', previoustrack: 'previous', play: 'play', pause: 'pause'}
		const setActionHandler = mediaSession.setActionHandler.bind(mediaSession)
		const sendAction = action => () => send({type: 'media-key', action: APP_MEDIA_ACTIONS[action]})
		mediaSession.setActionHandler = (action, handler) =>
			setActionHandler(action, action in APP_MEDIA_ACTIONS ? sendAction(action) : handler)
		for (const action in APP_MEDIA_ACTIONS)
			setActionHandler(action, sendAction(action))
	}


	const readyCheck = setInterval(() => {
		if (!window.ytcfg?.get?.('INNERTUBE_CONTEXT'))
			return
		clearInterval(readyCheck)
		send({type: 'ready', signedIn: Boolean(ytcfg.get('LOGGED_IN'))})
	}, READY_CHECK_MILLISECONDS)
	setInterval(watchPlayer, WATCH_MILLISECONDS)
})()
