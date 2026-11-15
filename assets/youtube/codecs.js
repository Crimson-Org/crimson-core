// Hides H.264 from YouTube so it streams VP9 or AV1, since the H.264 decoder WebKit uses fails on some videos
(() => {
	const HIDDEN_CODECS = /avc1|avc3|h264/i


	const isHidden = type => HIDDEN_CODECS.test(type ?? '')


	MediaSource.isTypeSupported = new Proxy(MediaSource.isTypeSupported, {
		apply: (check, owner, values) => !isHidden(values[0]) && Reflect.apply(check, owner, values),
	})
	HTMLMediaElement.prototype.canPlayType = new Proxy(HTMLMediaElement.prototype.canPlayType, {
		apply: (check, owner, values) => isHidden(values[0]) ? '' : Reflect.apply(check, owner, values),
	})
	if (navigator.mediaCapabilities?.decodingInfo) {
		const capabilities = navigator.mediaCapabilities
		capabilities.decodingInfo = new Proxy(capabilities.decodingInfo, {
			apply: (check, owner, values) => isHidden(values[0]?.video?.contentType)
				? Promise.resolve({supported: false, smooth: false, powerEfficient: false})
				: Reflect.apply(check, owner, values),
		})
	}
})()
