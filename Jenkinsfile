buildNumber = currentBuild.id
version = null
branchName = null
commitId = null
repoName = null

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
        stage( 'Prepare + Test + Build' ) {
            parallel {
                stage('Linux') {
                    agent {
                        dockerfile true
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH.tokenize( '/' ).drop( 1 ).join( '/' )
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()
                            repoUser = vcs.GIT_URL.tokenize( '/' )[-2]

                            if ( params.RELEASE_VERSION == null ) {
                                version = "0.0.${buildNumber}"
                            } else {
                                version = "${RELEASE_VERSION}.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh 'make dist_linux AVS_VERSION=' + version + ' BUILDVERSION=' + version
                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
                        sh 'zip -9j ./build/artifacts/zcall_linux_${version}.zip ./zcall'
                    }
                }
                stage('macOS') {
                    agent {
                        label 'built-in'
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH.tokenize( '/' ).drop( 1 ).join( '/' )
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()
                            repoUser = vcs.GIT_URL.tokenize( '/' )[-2]

                            if ( params.RELEASE_VERSION == null ) {
                                version = "0.0.${buildNumber}"
                            } else {
                                version = "${RELEASE_VERSION}.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        // build
                        sh 'mkdir -p ./contrib/webrtc/72.5/lib/wasm-generic'
                        sh 'touch ./contrib/webrtc/72.5/lib/wasm-generic/libwebrtc.a'

                        sh '. ./scripts/android_devenv.sh'
                        sh '. ./scripts/wasm_devenv.sh'
                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh 'make dist AVS_VERSION=' + version + ' BUILDVERSION=' + version

                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/osx/avs.framework.zip ./build/artifacts/avs.framework.osx.' + version + '.zip'
                        sh 'cp ./build/dist/ios/avs.xcframework.zip ./build/artifacts/avs.xcframework.zip'
                        sh 'zip -9j ./build/artifacts/avs.android.' + version + '.zip ./build/dist/android/avs.aar'
                        sh 'zip -9j ./build/artifacts/zcall_osx_' + version + '.zip ./zcall'
                        sh 'cp ./build/dist/wasm/wireapp-avs-' + version + '.tgz ./build/artifacts/'
                        // TODO: Add debug artifact
                        sh 'mkdir -p ./osx'
                        sh 'cp ./build/dist/osx/avscore.tar.bz2 ./osx'
                    }
                }
            }
        }
    }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
                }
            }
        }

        success {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    sh 'echo noop'
                    //wireSend secret: "$jenkinsbot_secret", message: "✅ avs ${ params.RELEASE_VERSION != null ? params.RELEASE_VERSION : 'main' } (${BUILD_ID}) succeeded\n${BUILD_URL}console\nhttps://${REPO_BASE_PATH}/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    sh 'echo noop'
                    //wireSend secret: "$jenkinsbot_secret", message: "❌ avs ${ params.RELEASE_VERSION != null ? params.RELEASE_VERSION : 'main' } (${BUILD_ID}) failed\n${BUILD_URL}console\nhttps://${REPO_BASE_PATH}/commit/${commitId}"
                }
            }
        }
    }
}
